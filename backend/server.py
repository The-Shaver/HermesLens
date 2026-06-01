"""HermesLens — FastAPI backend server.

Aggregates data from Hermes Agent sources and exposes a clean REST API
for the M5 StickS3 dashboard display.
"""

import logging
import traceback
from contextlib import suppress

import os
import sys
from pathlib import Path

# Ensure backend package is importable
sys.path.insert(0, str(Path(__file__).parent))

from config import (
    load_config,
    resolve_hermes_home,
    get_port,
    get_host,
    get_api_key,
    get_pages,
)

from sources.gateway import GatewayCollector
from sources.sessions import SessionsCollector
from sources.kanban import KanbanCollector
from sources.profiles import ProfilesCollector

try:
    from fastapi import FastAPI, HTTPException, Request
    from fastapi.middleware.cors import CORSMiddleware
    from fastapi.responses import JSONResponse
except ImportError:
    print("HermesLens requires fastapi and uvicorn.")
    print("Install with: pip install fastapi uvicorn[standard]")
    sys.exit(1)

logger = logging.getLogger("hermeslens")
if not logger.handlers:
    handler = logging.StreamHandler()
    handler.setFormatter(logging.Formatter("[%(levelname)s] %(message)s"))
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)

# Version
VERSION = "0.1.0"

# Create FastAPI app
app = FastAPI(
    title="HermesLens",
    version=VERSION,
    description="Hermes Agent physical dashboard API — data for M5 StickS3 display",
)

# CORS: allow any origin (the M5 StickS3 connects over local network)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Reusable collectors (lazy-initialized once)
_gateway: GatewayCollector | None = None
_sessions: SessionsCollector | None = None
_kanban: KanbanCollector | None = None
_profiles: ProfilesCollector | None = None
_config: dict | None = None


def _init_collectors() -> None:
    """Initialize or reinitialize all data collectors."""
    global _gateway, _sessions, _kanban, _profiles, _config
    _config = load_config()
    hermes_home = resolve_hermes_home()
    _gateway = GatewayCollector(hermes_home)
    _sessions = SessionsCollector(hermes_home)
    _kanban = KanbanCollector(hermes_home)
    _profiles = ProfilesCollector(hermes_home)


def _get_hermes_version() -> str:
    """Try to detect the Hermes Agent version from the installed package."""
    try:
        import subprocess
        result = subprocess.run(
            ["hermes", "--version"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            line = result.stdout.strip()
            # Extract version: "Hermes Agent v0.13.0 ..."
            for part in line.split():
                if part.startswith("v"):
                    return part
            return line
    except Exception:
        pass
    return "unknown"


@app.on_event("startup")
async def startup() -> None:
    """Initialize collectors on server start and log config summary."""
    _init_collectors()
    _log_startup_checks()
    _log_schema_contract()
    logger.info(
        "startup hermes_home=%s host=%s port=%s",
        resolve_hermes_home(),
        get_host(),
        get_port(),
    )


def _log_startup_checks() -> None:
    hermes_home = resolve_hermes_home()
    home = Path(hermes_home)
    if not home.exists() or not home.is_dir():
        logger.warning("hermes_home missing or not a directory: %s", hermes_home)
        return

    expected = [home / "state.db", home / "kanban.db", home / "gateway_state.json"]
    for path in expected:
        if path.exists():
            logger.info("found %s", path)
        else:
            logger.warning("missing %s", path)


def _log_schema_contract() -> None:
    logger.info(
        "schema contract /api/status version=%s",
        VERSION,
    )


@app.get("/api/health")
async def health() -> dict:
    """Simple liveness check."""
    return {
        "status": "ok",
        "version": VERSION,
    }


@app.get("/api/status")
async def get_status(request: Request) -> dict:
    """Main dashboard endpoint — returns all HermesLens data in one call.

    The M5 StickS3 polls this endpoint every N seconds to refresh the display.
    """
    api_key = get_api_key()
    if api_key:
        req_key = request.headers.get("X-API-Key", "")
        if req_key != api_key:
            raise HTTPException(status_code=401, detail="Invalid API key")

    if _gateway is None:
        _init_collectors()

    config = _config or load_config()

    gateway_data = _safe_collect(_gateway, "gateway", {"state": "error", "platforms": {}, "active_agents": 0})
    sessions_data = _safe_collect(
        _sessions,
        "sessions",
        {
            "total": 0,
            "active": 0,
            "today": 0,
            "recent": [],
            "tokens_input": 0,
            "tokens_output": 0,
            "tool_calls_total": 0,
            "estimated_cost_usd": 0.0,
        },
    )
    kanban_data = _safe_collect(
        _kanban,
        "kanban",
        {
            "ready": 0,
            "in_progress": 0,
            "blocked": 0,
            "done": 0,
            "archived": 0,
            "recent": [],
            "in_progress_with_assignee": [],
            "runs_recent": [],
        },
    )

    agent_filter = config.get("agents", []) if isinstance(config, dict) else []
    profiles_data = _safe_collect(
        _profiles,
        "profiles",
        {"agents": [{"name": "Default", "role": "System"}]},
        agent_filter=agent_filter or None,
    )

    in_progress_tasks = _safe_get(kanban_data, "in_progress_with_assignee", [])
    assignee_task_map = {}
    for task in in_progress_tasks:
        assignee = (task.get("assignee") or "").strip()
        if not assignee:
            continue
        if assignee not in assignee_task_map:
            assignee_task_map[assignee] = (task.get("title") or "Working...")[:60]

    for agent in profiles_data.get("agents", []):
        name = agent.get("name", "")
        task = assignee_task_map.get(name)
        agent["status"] = "active" if task else "idle"
        agent["task_progress"] = 50 if task else 0
        agent["current_task"] = task or ""
        agent["model"] = ""

    return {
        "version": VERSION,
        "hermes_version": _get_hermes_version(),
        "hermes_home": resolve_hermes_home(),
        "config": {
            "refresh_interval": config.get("refresh_interval", 10) if isinstance(config, dict) else 10,
            "pages": config.get("pages", ["agents", "tasks", "system", "usage"]) if isinstance(config, dict) else ["agents", "tasks", "system", "usage"],
        },
        "gateway": gateway_data,
        "sessions": sessions_data,
        "tasks": kanban_data,
        "agents": profiles_data,
        "current_model": (sessions_data.get("recent") or [{}])[0].get("model") or "",
        "session_cost_usd": _session_cost(sessions_data),
    }

    payload = {
        "version": VERSION,
        "hermes_version": _get_hermes_version(),
        "hermes_home": resolve_hermes_home(),
        "config": {
            "refresh_interval": config.get("refresh_interval", 10) if isinstance(config, dict) else 10,
            "pages": config.get("pages", ["agents", "tasks", "system", "usage"]) if isinstance(config, dict) else ["agents", "tasks", "system", "usage"],
        },
        "gateway": gateway_data,
        "sessions": sessions_data,
        "tasks": kanban_data,
        "agents": profiles_data,
        "current_model": (sessions_data.get("recent") or [{}])[0].get("model") or "",
        "session_cost_usd": _session_cost(sessions_data),
    }

    try:
        _validate_status_response(payload)
    except Exception as exc:
        logger.exception("/api/status schema validation failed: %s", exc)

    return payload


def _safe_collect(collector, name: str, default: dict, **kwargs):
    """Run collector.collect(), log tracebacks, return defaults on failure."""
    try:
        if kwargs:
            return collector.collect(**kwargs)
        return collector.collect()
    except Exception:
        logger.exception("collector failed: %s", name)
        return default


def _safe_get(data: dict, key: str, default):
    try:
        value = data.get(key, default)
        return value if value is not None else default
    except Exception:
        return default


def _session_cost(sessions_data: dict) -> float:
    try:
        value = sessions_data.get("session_cost_usd")
        if value is not None:
            return float(value)
        recent = sessions_data.get("recent") or [{}]
        value = (recent[0].get("estimated_cost_usd"))
        if value is not None:
            return float(value)
    except Exception:
        pass
    return 0.0


def _validate_status_response(data: dict) -> None:
    expected_keys = {
        "version": str,
        "hermes_version": str,
        "hermes_home": str,
        "config": dict,
        "gateway": dict,
        "sessions": dict,
        "tasks": dict,
        "agents": dict,
        "current_model": str,
        "session_cost_usd": float,
    }

    missing = [k for k in expected_keys if k not in data]
    if missing:
        logger.error("/api/status response missing keys: %s", ", ".join(missing))
        raise ValueError(f"/api/status missing keys: {', '.join(missing)}")

    if not isinstance(data.get("config"), dict):
        raise ValueError("/api/status config must be a mapping")
    if not isinstance(data.get("gateway"), dict):
        raise ValueError("/api/status gateway must be a mapping")
    if not isinstance(data.get("sessions"), dict):
        raise ValueError("/api/status sessions must be a mapping")
    if not isinstance(data.get("tasks"), dict):
        raise ValueError("/api/status tasks must be a mapping")
    if not isinstance(data.get("agents"), dict):
        raise ValueError("/api/status agents must be a mapping")
    if "agents" in data.get("agents", {}) and not isinstance(data["agents"].get("agents"), list):
        raise ValueError("/api/status agents.agents must be a list")


@app.exception_handler(Exception)
async def global_exception_handler(request: Request, exc: Exception) -> JSONResponse:
    """Catch-all error handler — never return 500 to the M5 StickS3."""
    logger.exception("unhandled exception on %s", request.url.path)
    return JSONResponse(
        status_code=500,
        content={
            "error": "Internal server error",
            "detail": str(exc),
        },
    )


def main() -> None:
    """Entry point for running the server."""
    import uvicorn

    port = get_port()
    host = get_host()

    print("╔══════════════════════════════════════════╗")
    print("║         HermesLens v{}             ║".format(VERSION))
    print("║  Physical Dashboard for Hermes Agent     ║")
    print("╠══════════════════════════════════════════╣")
    print("║  Server: http://{}:{}            ".format(host, port))
    print("║  API:    http://{}:{}/api/status ".format(host, port))
    print("║  Health: http://{}:{}/api/health ".format(host, port))
    print("╚══════════════════════════════════════════╝")

    uvicorn.run(app, host=host, port=port, log_level="info")


if __name__ == "__main__":
    main()
