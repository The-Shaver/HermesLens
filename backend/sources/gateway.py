"""Gateway state data collector."""

import json
from pathlib import Path


class GatewayCollector:
    """Collects gateway state from ~/.hermes/gateway_state.json.

    Reads the JSON file to extract:
    - gateway_state (running/stopped/etc)
    - platforms (connection status per platform like discord, telegram)
    - active_agents count
    - pid and other metadata
    """
    
    def __init__(self, hermes_home: str):
        self.hermes_home = Path(hermes_home).expanduser()
        self.gateway_path = self.hermes_home / "gateway_state.json"
    
    def collect(self) -> dict:
        """Collect gateway state data.

        Returns a dictionary with gateway information, or defaults if file doesn't exist.
        """
        default_result = {
            "state": "unknown",
            "platforms": {},
            "active_agents": 0,
        }
        
        try:
            if not self.gateway_path.exists():
                return default_result
            
            with open(self.gateway_path) as f:
                data = json.load(f)
            
            result = {
                "state": data.get("gateway_state", "unknown"),
                "platforms": {k: v.get("state", "disconnected") for k, v in data.get("platforms", {}).items()},
                "active_agents": data.get("active_agents", 0),
            }
            
            # Include additional metadata if available
            result["pid"] = data.get("pid")
            result["updated_at"] = data.get("updated_at")
            result["kind"] = data.get("kind")
            
            return result
            
        except (json.JSONDecodeError, IOError) as e:
            # Return defaults on any error
            return default_result
