"""Gateway state data collector."""

import json
import logging
from pathlib import Path

logger = logging.getLogger("hermeslens")

_GATEWAY_SCHEMA = {
    "state": str,
    "platforms": dict,
    "active_agents": int,
    "pid": (int, type(None)),
    "updated_at": (str, type(None)),
    "kind": (str, type(None)),
}


class GatewayCollector:
    """Collects gateway state from ~/.hermes/gateway_state.json."""

    def __init__(self, hermes_home: str):
        self.hermes_home = Path(hermes_home).expanduser()
        self.gateway_path = self.hermes_home / "gateway_state.json"

    def collect(self) -> dict:
        default_result = {
            "state": "unknown",
            "platforms": {},
            "active_agents": 0,
        }

        try:
            if not self.gateway_path.exists():
                logger.warning("gateway state file missing: %s", self.gateway_path)
                return default_result

            with open(self.gateway_path) as f:
                data = json.load(f)

            if not isinstance(data, dict):
                logger.error("gateway state file is not a mapping: %s", self.gateway_path)
                return default_result

            result = {
                "state": data.get("gateway_state", "unknown"),
                "platforms": {k: v.get("state", "disconnected") if isinstance(v, dict) else "disconnected"
                              for k, v in data.get("platforms", {}).items()},
                "active_agents": data.get("active_agents", 0),
            }

            for key, typ in _GATEWAY_SCHEMA.items():
                if key in ("state", "platforms", "active_agents"):
                    continue
                if key in data:
                    result[key] = data[key]

            return result

        except (json.JSONDecodeError, IOError) as exc:
            logger.error("gateway collect failed: %s", exc)
            return default_result
        except Exception as exc:
            logger.exception("gateway collect unexpected error: %s", exc)
            return default_result
