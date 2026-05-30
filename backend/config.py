"""Config loader for HermesLens backend."""

import os
from pathlib import Path


DEFAULT_CONFIG = {
    "hermes_home": "~/.hermes",  # auto-detect via $HERMES_HOME or fallback
    "port": 8123,
    "host": "0.0.0.0",  # listen on all interfaces so the M5 StickS3 can reach it
    "refresh_interval": 10,  # seconds for polling
    "agents": [],  # empty = show all discovered profiles
    "api_key": None,  # no auth by default
    "pages": ["agents", "tasks", "system", "usage"],
}

# Load config from file or env var
_CONFIG_PATH: Path | None = None


def get_config_path() -> str | None:
    """Get the path to the HermesLens config file."""
    return _CONFIG_PATH


def load_config() -> dict[str, any]:
    """Load and merge configuration with defaults.

    Returns a merged config dictionary with all values resolved.
    """
    global _CONFIG_PATH
    
    # Check environment variable first
    env_path = os.environ.get("HERMESLENS_CONFIG")
    if env_path:
        _CONFIG_PATH = Path(env_path)
    
    # Try to load from file
    # Start from defaults so callers always get a usable config
    config_data: dict[str, any] = dict(DEFAULT_CONFIG)
    
    # Try to load from file (overrides defaults)
    if _CONFIG_PATH and _CONFIG_PATH.exists():
        try:
            import yaml
            with open(_CONFIG_PATH) as f:
                loaded_config = yaml.safe_load(f) or {}
            config_data = {**DEFAULT_CONFIG, **loaded_config}
        except Exception:
            # If YAML parsing fails, use defaults
            pass
    
    # Apply environment variable overrides for specific keys
    if "port" in os.environ:
        try:
            config_data["port"] = int(os.environ["port"])
        except ValueError:
            pass  # Keep default or loaded value
    
    if "host" in os.environ:
        config_data["host"] = os.environ["host"]
    
    if "api_key" in os.environ:
        config_data["api_key"] = os.environ["api_key"]
    
    # Resolve ~ to full path for hermes_home
    if config_data.get("hermes_home", "").startswith("~"):
        config_data["hermes_home"] = str(Path(config_data["hermes_home"]).expanduser())
    
    return config_data


def resolve_hermes_home() -> str:
    """Get the resolved Hermes home path.

    Checks $HERMES_HOME env var first, then falls back to config,
    then to ~/.hermes by default.
    """
    # Check environment variable first
    if "HERMES_HOME" in os.environ:
        return os.environ["HERMES_HOME"]
    
    # Try config file
    cfg = load_config()
    if cfg.get("hermes_home"):
        return cfg["hermes_home"]
    
    # Default fallback
    return str(Path("~/.hermes").expanduser())


def get_port() -> int:
    """Get the configured server port."""
    return load_config().get("port", 8123)


def get_host() -> str:
    """Get the configured server host."""
    cfg = load_config()
    return cfg.get("host", "127.0.0.1")


def get_api_key() -> str | None:
    """Get the configured API key for authentication (if any)."""
    return load_config().get("api_key") or os.environ.get("HERMESLENS_API_KEY")


def get_pages() -> list[str]:
    """Get the list of page names to display."""
    cfg = load_config()
    pages = cfg.get("pages", [])
    if not pages:
        return ["agents", "tasks", "system", "usage"]
    return pages
