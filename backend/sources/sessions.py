"""Sessions data collector."""

import logging
import sqlite3
import time
from datetime import datetime, timezone
from pathlib import Path

logger = logging.getLogger("hermeslens")

_SESSIONS_SCHEMA = {
    "total": int,
    "active": int,
    "today": int,
    "recent": list,
    "tokens_input": int,
    "tokens_output": int,
    "tool_calls_total": int,
    "estimated_cost_usd": float,
    "session_cost_usd": float,
}


class SessionsCollector:
    """Collects session statistics from ~/.hermes/state.db."""

    def __init__(self, hermes_home: str):
        self.hermes_home = Path(hermes_home).expanduser()
        self.state_path = self.hermes_home / "state.db"
        self.conn: sqlite3.Connection | None = None

    def _get_conn(self) -> sqlite3.Connection | None:
        if self.conn is None or not self.conn:
            try:
                self.conn = sqlite3.connect(str(self.state_path))
            except sqlite3.OperationalError as exc:
                logger.error("sessions db connect failed: %s", exc)
                return None
        return self.conn

    def _close_conn(self) -> None:
        if self.conn and not self.conn.closed:
            try:
                self.conn.close()
            except Exception as exc:
                logger.debug("sessions db close failed: %s", exc)
            finally:
                self.conn = None

    def collect(self) -> dict:
        default_result = {k: (0 if typ is not float else 0.0) for k, typ in _SESSIONS_SCHEMA.items()}
        default_result["recent"] = []

        try:
            conn = self._get_conn()
            if conn is None or not conn:
                return default_result

            cursor = conn.cursor()

            cursor.execute("SELECT COUNT(*) FROM sessions")
            total_sessions = cursor.fetchone()[0] or 0

            cursor.execute("SELECT COUNT(*) FROM sessions WHERE ended_at IS NULL")
            active_sessions = cursor.fetchone()[0] or 0

            now = time.time()
            today_start = datetime.now(timezone.utc).replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
            cursor.execute(
                "SELECT COUNT(*) FROM sessions WHERE started_at >= ?",
                (today_start,),
            )
            sessions_today = cursor.fetchone()[0] or 0

            cursor.execute(
                "SELECT COALESCE(SUM(input_tokens), 0) as input, "
                "COALESCE(SUM(output_tokens), 0) as output, "
                "COALESCE(SUM(tool_call_count), 0) as tool_calls "
                "FROM sessions"
            )
            agg = cursor.fetchone()
            tokens_input, tokens_output, tool_calls_total = agg or (0, 0, 0)

            cursor.execute(
                "SELECT COALESCE(SUM(estimated_cost_usd), 0) "
                "FROM sessions WHERE cost_status != 'unknown' AND estimated_cost_usd IS NOT NULL"
            )
            cost_agg = cursor.fetchone()
            overall_cost = cost_agg[0] if cost_agg else 0.0

            cursor.execute(
                "SELECT title, model, source, started_at, ended_at, message_count, "
                "input_tokens, output_tokens, estimated_cost_usd, cost_status "
                "FROM sessions ORDER BY started_at DESC LIMIT 10"
            )

            recent_sessions = []
            for row in cursor.fetchall():
                try:
                    title, model, source, started_at, ended_at, message_count, input_tokens, output_tokens, estimated_cost, cost_status = row
                except Exception as exc:
                    logger.debug("sessions row unpack failed: %s", exc)
                    continue

                recent_sessions.append(
                    {
                        "title": title or "Untitled",
                        "model": model or None,
                        "source": source or None,
                        "started_at": datetime.fromtimestamp(started_at).isoformat().replace("+00:00", "Z") if started_at else None,
                        "ended_at": datetime.fromtimestamp(ended_at).isoformat().replace("+00:00", "Z") if ended_at else None,
                        "message_count": message_count or 0,
                        "input_tokens": int(input_tokens) if input_tokens is not None else 0,
                        "output_tokens": int(output_tokens) if output_tokens is not None else 0,
                        "estimated_cost_usd": float(estimated_cost) if estimated_cost is not None and estimated_cost != "unknown" else None,
                        "cost_status": cost_status or None,
                    }
                )

            session_cost = 0.0
            if recent_sessions and recent_sessions[0].get("estimated_cost_usd") is not None:
                session_cost = float(recent_sessions[0]["estimated_cost_usd"])

            return {
                "total": int(total_sessions),
                "active": int(active_sessions),
                "today": int(sessions_today),
                "recent": recent_sessions,
                "tokens_input": int(tokens_input),
                "tokens_output": int(tokens_output),
                "tool_calls_total": int(tool_calls_total),
                "estimated_cost_usd": float(overall_cost) if overall_cost is not None else 0.0,
                "session_cost_usd": session_cost,
            }

        except (sqlite3.Error, IOError) as exc:
            logger.error("sessions collect failed: %s", exc)
            return default_result
        except Exception as exc:
            logger.exception("sessions collect unexpected error: %s", exc)
            return default_result
        finally:
            self._close_conn()
