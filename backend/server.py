"""HermesLens — FastAPI backend server.

Aggregates data from Hermes Agent sources and exposes a clean REST API
for the M5 StickS3 dashboard display.
"""

import json
import os
import sys
import signal
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


def _init_collectors():
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
            capture_output=True, text=True, timeout=5
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
async def startup():
    """Initialize collectors on server start."""
    _init_collectors()


@app.get("/api/health")
async def health():
    """Simple liveness check."""
    return {
        "status": "ok",
        "version": VERSION,
    }


@app.get("/api/status")
async def get_status(request: Request):
    """Main dashboard endpoint — returns all HermesLens data in one call.

    The M5 StickS3 polls this endpoint every N seconds to refresh the display.
    """
    # --- Auth check ---
    api_key = get_api_key()
    if api_key:
        req_key = request.headers.get("X-API-Key", "")
        if req_key != api_key:
            raise HTTPException(status_code=401, detail="Invalid API key")

    # Ensure collectors are initialized
    if _gateway is None:
        _init_collectors()

    # --- Collect from all sources ---
    try:
        gateway_data = _gateway.collect()
    except Exception:
        gateway_data = {"state": "error", "platforms": {}, "active_agents": 0}

    try:
        sessions_data = _sessions.collect()
    except Exception:
        sessions_data = {"total": 0, "active": 0, "today": 0, "recent": [],
                         "tokens_input": 0, "tokens_output": 0,
                         "tool_calls_total": 0, "estimated_cost_usd": 0.0}

    try:
        kanban_data = _kanban.collect_all()
    except Exception:
        kanban_data = {"ready": 0, "in_progress": 0, "blocked": 0, "done": 0,
                        "archived": 0, "recent": [],
                        "in_progress_with_assignee": [], "runs_recent": []}

    try:
        agent_filter = _config.get("agents", []) if _config else []
        profiles_data = _profiles.collect(agent_filter=agent_filter or None)
    except Exception:
        profiles_data = {"agents": [{"name": "Default", "role": "System"}]}

    # --- Build response ---
    return {
        "version": VERSION,
        "hermes_version": _get_hermes_version(),
        "hermes_home": resolve_hermes_home(),
        "config": {
            "refresh_interval": _config.get("refresh_interval", 10) if _config else 10,
            "pages": _config.get("pages", ["agents", "tasks", "system", "usage"]) if _config else ["agents", "tasks", "system", "usage"],
        },
        "gateway": gateway_data,
        "sessions": sessions_data,
        "tasks": kanban_data,
        "agents": profiles_data,
    }


@app.exception_handler(Exception)
async def global_exception_handler(request: Request, exc: Exception):
    """Catch-all error handler — never return 500 to the M5 StickS3."""
    return JSONResponse(
        status_code=500,
        content={
            "error": "Internal server error",
            "detail": str(exc),
        },
    )


def main():
    """Entry point for running the server."""
    import uvicorn

    port = get_port()
    host = get_host()

    print(f"╔══════════════════════════════════════════╗")
    print(f"║         HermesLens v{VERSION}             ║")
    print(f"║  Physical Dashboard for Hermes Agent     ║")
    print(f"╠══════════════════════════════════════════╣")
    print(f"║  Server: http://{host}:{port}            ")
    print(f"║  API:    http://{host}:{port}/api/status ")
    print(f"║  Health: http://{host}:{port}/api/health ")
    print(f"╚══════════════════════════════════════════╝")

    uvicorn.run(app, host=host, port=port, log_level="info")


if __name__ == "__main__":
    main()
