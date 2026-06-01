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

KNOWN_PAGES = {"agents", "tasks", "system", "usage"}

# Load config from file or env var
_CONFIG_PATH: Path | None = None


def get_config_path() -> str | None:
    """Get the path to the HermesLens config file."""
    return _CONFIG_PATH


def _coerce_port(value) -> int:
    try:
        port = int(value)
    except (TypeError, ValueError):
        raise ValueError(f"Invalid port: {value!r}")
    if not (1 <= port <= 65535):
        raise ValueError(f"Port out of range: {port}")
    return port


def _coerce_refresh_interval(value) -> int:
    try:
        interval = int(value)
    except (TypeError, ValueError):
        raise ValueError(f"Invalid refresh_interval: {value!r}")
    if interval <= 0:
        raise ValueError(f"refresh_interval must be positive: {interval}")
    return interval


def _coerce_pages(value) -> list[str]:
    if not value:
        return list(DEFAULT_CONFIG["pages"])
    if not isinstance(value, list):
        raise ValueError(f"pages must be a list, got {type(value).__name__}")
    cleaned = []
    for page in value:
        if not isinstance(page, str):
            raise ValueError(f"page entry must be str, got {type(page).__name__}")
        page = page.strip().lower()
        if page not in KNOWN_PAGES:
            raise ValueError(f"Unknown page name: {page!r}")
        cleaned.append(page)
    if not cleaned:
        return list(DEFAULT_CONFIG["pages"])
    return cleaned


def _validate(config_data: dict) -> None:
    """Raise ValueError on invalid config values."""
    if "port" in config_data:
        config_data["port"] = _coerce_port(config_data["port"])
    if "refresh_interval" in config_data:
        config_data["refresh_interval"] = _coerce_refresh_interval(config_data["refresh_interval"])
    if "pages" in config_data:
        config_data["pages"] = _coerce_pages(config_data["pages"])
    if "api_key" in config_data and config_data["api_key"] is not None:
        if not isinstance(config_data["api_key"], str) or not config_data["api_key"].strip():
            raise ValueError("api_key must be a non-empty string")


def load_config() -> dict:
    """Load and merge configuration with defaults.

    Returns a merged config dictionary with all values resolved.
    Raises ValueError if the config file or env overrides are invalid.
    """
    global _CONFIG_PATH

    env_path = os.environ.get("HERMESLENS_CONFIG")
    if env_path:
        _CONFIG_PATH = Path(env_path)

    config_data: dict = dict(DEFAULT_CONFIG)

    if _CONFIG_PATH and _CONFIG_PATH.exists():
        try:
            import yaml
            with open(_CONFIG_PATH) as f:
                loaded_config = yaml.safe_load(f) or {}
            if not isinstance(loaded_config, dict):
                raise ValueError("Config root must be a mapping")
            config_data = {**DEFAULT_CONFIG, **loaded_config}
        except ValueError:
            raise
        except Exception as exc:
            raise ValueError(f"Failed to load config: {exc}")

    # Env overrides: prefer standard uppercase names, accept lowercase as fallback
    port_env = os.environ.get("PORT", os.environ.get("port"))
    if port_env is not None:
        config_data["port"] = _coerce_port(port_env)

    host_env = os.environ.get("HOST", os.environ.get("host"))
    if host_env is not None:
        config_data["host"] = str(host_env).strip()

    api_key_env = os.environ.get("HERMESLENS_API_KEY", os.environ.get("api_key"))
    if api_key_env is not None:
        config_data["api_key"] = str(api_key_env).strip() or None

    _validate(config_data)

    # Resolve ~ to full path for hermes_home
    hermes_home = config_data.get("hermes_home", "")
    if isinstance(hermes_home, str) and hermes_home.startswith("~"):
        config_data["hermes_home"] = str(Path(hermes_home).expanduser())

    return config_data


def resolve_hermes_home() -> str:
    """Get the resolved Hermes home path.

    Checks $HERMES_HOME env var first, then falls back to config,
    then to ~/.hermes by default.
    """
    if "HERMES_HOME" in os.environ:
        return os.environ["HERMES_HOME"]

    cfg = load_config()
    home = cfg.get("hermes_home")
    if home:
        return str(home)

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
        return list(DEFAULT_CONFIG["pages"])
    return pages
