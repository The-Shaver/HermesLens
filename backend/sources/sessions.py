"""Sessions data collector."""

import sqlite3
import time
from datetime import datetime, timezone
from pathlib import Path


class SessionsCollector:
    """Collects session statistics from ~/.hermes/state.db.

    Queries the sessions table to extract:
    - Total session count
    - Active (running) sessions
    - Sessions started today
    - Recent sessions (last 10) with details
    - Aggregated token counts and costs
    """
    
    def __init__(self, hermes_home: str):
        self.hermes_home = Path(hermes_home).expanduser()
        self.state_path = self.hermes_home / "state.db"
        self.conn: sqlite3.Connection | None = None
    
    def _get_conn(self) -> sqlite3.Connection:
        """Get a database connection, creating it if needed."""
        if self.conn is None or not self.conn:
            try:
                self.conn = sqlite3.connect(str(self.state_path))
            except sqlite3.OperationalError as e:
                # Database might be locked - return None to indicate we can't connect
                pass
        return self.conn
    
    def _close_conn(self):
        """Close the database connection if open."""
        if self.conn and not self.conn.closed:
            try:
                self.conn.close()
            except Exception:
                pass
            finally:
                self.conn = None
    
    def collect(self) -> dict:
        """Collect session data.

        Returns a dictionary with session statistics, or defaults if database is unavailable.
        """
        default_result = {
            "total": 0,
            "active": 0,
            "today": 0,
            "recent": [],
            "tokens_input": 0,
            "tokens_output": 0,
            "tool_calls_total": 0,
            "estimated_cost_usd": 0.0,
        }
        
        try:
            conn = self._get_conn()
            if conn is None or not conn:
                return default_result
            
            cursor = conn.cursor()
            
            # Total sessions count
            cursor.execute("SELECT COUNT(*) FROM sessions")
            total_sessions = cursor.fetchone()[0]
            
            # Active sessions (ended_at IS NULL)
            cursor.execute("SELECT COUNT(*) FROM sessions WHERE ended_at IS NULL")
            active_sessions = cursor.fetchone()[0]
            
            # Sessions started today (started_at is a Unix timestamp)
            now = time.time()
            today_start = datetime.now(timezone.utc).replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
            cursor.execute("""
                SELECT COUNT(*) FROM sessions 
                WHERE started_at >= ?
            """, (today_start,))
            sessions_today = cursor.fetchone()[0]
            
            # Aggregate token counts
            cursor.execute("""
                SELECT 
                    COALESCE(SUM(input_tokens), 0) as input,
                    COALESCE(SUM(output_tokens), 0) as output,
                    COALESCE(SUM(tool_call_count), 0) as tool_calls
                FROM sessions
            """)
            agg = cursor.fetchone()
            
            # Aggregate estimated costs where cost_status is not 'unknown'
            cursor.execute("""
                SELECT COALESCE(SUM(estimated_cost_usd), 0) 
                FROM sessions 
                WHERE cost_status != 'unknown' AND estimated_cost_usd IS NOT NULL
            """)
            cost_agg = cursor.fetchone()
            
            # Get recent sessions (last 10 by started_at desc)
            cursor.execute("""
                SELECT 
                    title, model, source, started_at, ended_at, message_count,
                    input_tokens, output_tokens, estimated_cost_usd, cost_status
                FROM sessions 
                ORDER BY started_at DESC 
                LIMIT 10
            """)
            
            recent_sessions = []
            for row in cursor.fetchall():
                title = row[0] if row[0] else "Untitled"
                model = str(row[1]) if row[1] else None
                source = str(row[2]) if row[2] else None
                started_at = row[3]  # timestamp
                ended_at = row[4]    # timestamp or null
                message_count = row[5]
                input_tokens = int(row[6]) if row[6] is not None else 0
                output_tokens = int(row[7]) if row[7] is not None else 0
                estimated_cost = float(row[8]) if row[8] is not None and row[8] != "unknown" else None
                cost_status = str(row[9]) if row[9] else None
                
                recent_sessions.append({
                    "title": title,
                    "model": model,
                    "source": source,
                    "started_at": datetime.fromtimestamp(started_at).isoformat().replace("+00:00", "Z") if started_at else None,
                    "ended_at": datetime.fromtimestamp(ended_at).isoformat().replace("+00:00", "Z") if ended_at else None,
                    "message_count": message_count,
                    "input_tokens": input_tokens,
                    "output_tokens": output_tokens,
                    "estimated_cost_usd": estimated_cost,
                    "cost_status": cost_status,
                })
            
            return {
                "total": total_sessions,
                "active": active_sessions,
                "today": sessions_today,
                "recent": recent_sessions,
                "tokens_input": int(agg[0]),
                "tokens_output": int(agg[1]),
                "tool_calls_total": int(agg[2]),
                "estimated_cost_usd": float(cost_agg[0]) if cost_agg and cost_agg[0] is not None else 0.0,
            }
            
        except (sqlite3.Error, IOError) as e:
            # Return defaults on any database error
            return default_result
