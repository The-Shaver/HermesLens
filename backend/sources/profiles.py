"""Profiles auto-discovery collector."""

import os
from pathlib import Path


class ProfilesCollector:
    """Collects agent profile information from ~/.hermes/profiles/.

    Scans the profiles directory to discover agents and extract:
    - Agent name from config.yaml (agent.name field)
    - Role hints from AGENTS.md if available
    - SOUL.md existence marker
    """
    
    def __init__(self, hermes_home: str):
        self.hermes_home = Path(hermes_home).expanduser()
        self.profiles_path = self.hermes_home / "profiles"
        # Default fallback if profiles directory doesn't exist
        self.default_agent_name = "Default"
    
    def collect(self, agent_filter: list[str] | None = None) -> dict:
        """Collect profile data.

        Args:
            agent_filter: Optional list of agent names to include (if specified in config).

        Returns a dictionary with discovered agents or defaults if no profiles found.
        """
        default_result = {
            "agents": [{"name": self.default_agent_name, "role": "System", "current_task": None}],
        }
        
        try:
            # Check if profiles directory exists
            if not self.profiles_path.exists():
                return default_result
            
            agents = []
            
            # Scan profile directories
            for item in sorted(self.profiles_path.iterdir()):
                if not item.is_dir() or item.name.startswith('.'):
                    continue
                
                config_path = item / "config.yaml"
                
                agent_info = {
                    "name": None,  # Will be set from config or use directory name as fallback
                    "role": None,
                    "current_task": None,
                    "has_soul": False,
                }
                
                # Try to read config.yaml for agent.name
                if config_path.exists():
                    try:
                        import yaml
                        with open(config_path) as f:
                            data = yaml.safe_load(f) or {}
                        
                        # Try different possible name locations
                        agent_name = None
                        
                        # First check for agent.name (nested structure)
                        if "agent" in data and isinstance(data["agent"], dict):
                            agent_name = data["agent"].get("name")
                        
                        # Fall back to top-level name field
                        if not agent_name:
                            agent_name = data.get("name")
                        
                        # Fall back to directory name
                        if not agent_name:
                            agent_name = item.name
                        
                        agent_info["name"] = agent_name
                        
                        # Extract role from config (role, type, or derived)
                        agent_role = None
                        if "agent" in data and isinstance(data["agent"], dict):
                            agent_role = data["agent"].get("role")
                        elif "type" in data:
                            agent_role = data["type"]
                        
                        # Read AGENTS.md for role hints if available
                        agents_md_path = item / "AGENTS.md"
                        if not agent_role and agents_md_path.exists():
                            try:
                                with open(agents_md_path) as f:
                                    content = f.read()
                                if "coder-reviewer" in content.lower():
                                    agent_role = "Coder/Reviewer"
                                elif "orchestrator" in content.lower():
                                    agent_role = "Orchestrator"
                                elif "writer" in content.lower():
                                    agent_role = "Content Writer"
                            except Exception:
                                pass
                        
                        if agent_role:
                            agent_info["role"] = agent_role
                        
                        # Check for SOUL.md
                        soul_path = item / "SOUL.md"
                        if soul_path.exists():
                            agent_info["has_soul"] = True
                        
                    except (yaml.YAMLError, IOError):
                        # YAML parsing error - use directory name as fallback
                        pass
                
                # Apply filter if specified
                if not agent_filter or agent_info["name"] in agent_filter:
                    agents.append(agent_info)
            
            # If no agents found but we have a default, create one
            if not agents:
                return {
                    "agents": [{"name": self.default_agent_name, "role": "System", "current_task": None}],
                }
            
            return {"agents": agents}
            
        except Exception as e:
            # Return defaults on any error
            return default_result
