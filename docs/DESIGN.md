# HermesLens — Design Document

## Project Overview

HermesLens is an open-source physical dashboard for Hermes Agent that runs on an M5 StickS3 device. It provides a real-time, glanceable view of your agent team, tasks, system health, and usage stats.

The backend is a FastAPI service that reads live Hermes data from SQLite and local files, then exposes it over a simple HTTP API. The M5 StickS3 polls that API and renders a 4-page dashboard on its built-in TFT.

---

## Where config lives

- Device config: stored in ESP32 NVS via the captive portal (WiFi SSID, WiFi password, backend URL). No YAML, no filesystem config on the device.
- Backend config: none required beyond the path to the Hermes home directory. Defaults: host `0.0.0.0`, port `8123`.
- No standalone `hermeslens` package or `pip install`. The backend runs from the repo (`python server.py`).

---

## Data Flow

1. M5 StickS3 connects to WiFi.
2. It polls `GET /api/status` on the backend every ~10 seconds.
3. The backend reads from `~/.hermes/state.db`, `~/.hermes/gateway_state.json`, and other Hermes data sources.
4. The device renders Agents, Tasks, System, and Usage pages.

---

## UI Layout (4 Pages + Navigation)

| # | Page | Content |
|---|------|---------|
| 1 | Agents | Agent cards: name, role, status dot, progress, current task |
| 2 | Tasks | Kanban summary: counts by status (ready/active/blocked/done) |
| 3 | System | Gateway state, platforms, active sessions, cron jobs |
| 4 | Usage | Active model, session cost, overall estimated cost, session count |

**Navigation:**
- **Front button (short)** → Next page
- **Side button (short)** → Previous page
- **Front button (long)** → Detail overlay for current item
- **Side button (long)** → Jump to Agents page (home)

---

## API Endpoints

The HermesLens backend exposes a FastAPI server:

| Endpoint | Description |
|----------|-------------|
| `GET /api/health` | Backend liveness check |
| `GET /api/status` | Full dashboard data (all pages in one call) |

### `/api/status` Response Shape

```json
{
  "version": "0.1.1",
  "current_model": "stepfun/step-3.5-flash",
  "gateway": {
    "state": "running",
    "platforms": {
      "discord": "connected",
      "telegram": "disconnected"
    }
  },
  "agents": [
    {
      "name": "timmy",
      "role": "Ghost CMS",
      "status": "active",
      "current_task": "Deploying theme fix",
      "task_progress": 42,
      "model": "stepfun/step-3.5-flash"
    }
  ],
  "tasks": {
    "ready": 3,
    "in_progress": 2,
    "blocked": 1,
    "done": 12,
    "recent": [
      {"title": "Install theme", "status": "done", "assignee": "timmy"},
      {"title": "Photo pipeline", "status": "in_progress", "assignee": "cooper"}
    ]
  },
  "system": {
    "gateway_state": "running",
    "platforms_connected": ["discord"],
    "active_sessions": 3,
    "cron_jobs": [
      {"name": "Blog check", "schedule": "every 2h", "next_run": "16:00"}
    ]
  },
  "usage": {
    "model": "stepfun/step-3.5-flash",
    "sessions_total": 247,
    "messages_total": 3891,
    "tool_calls_total": 843,
    "tokens_input": 1200000,
    "tokens_output": 456000,
    "estimated_cost_usd": 0.87,
    "session_cost_usd": 0.00
  }
}
```

Notes:
- `current_model` is derived from the most recent session.
- `session_cost_usd` is the current session cost, falling back to the most recent session if no open session exists.
- `agents` entries are enriched server-side by joining agent profiles with in-progress kanban tasks.

---

## Data Sources

| Source | Path | Data |
|--------|------|------|
| Gateway state | `~/.hermes/gateway_state.json` | Running state, connected platforms |
| Sessions | `~/.hermes/state.db` | Recent sessions, messages, token counts, cost |
| Kanban tasks | `~/.hermes/kanban.db` | Task title, assignee, status, priority, recent runs |
| Cron jobs | `~/.hermes/cron/` | Job schedules, scripts |
| Profiles | `~/.hermes/profiles/` | Agent names, roles |

**Hermes home is read-only**. HermesLens only reads data; it never writes to `~/.hermes`.

---

## Project Structure

```
hermeslens/
  backend/
    server.py           # FastAPI entry point (`python server.py`)
    sources/            # Data collectors for gateway, sessions, kanban, profiles
  firmware-cpp/
    src/                # C++/PlatformIO app (WiFi, captive portal, display, API client)
  docs/
    hardware-setup.md   # M5 StickS3 assembly and flashing
  README.md
  PLAN.md
  CURRENT_STATE.md
  TRACKED_ISSUES.md
```

---

## Flash Layout

End users flash the M5 StickS3 with three files:

| File | Offset |
|------|--------|
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x00010000` |

- Single-file merge was investigated and deemed unreliable; this repo keeps the 3-file process.
- No erase step is required before flashing.

---

## Phases

| Phase | Name | Status |
|-------|------|--------|
| 0 | Design | Done |
| 1 | Backend data service | Working |
| 2 | Firmware: portal + config + display | Working |
| 3 |Polish + backend hardening + end-to-end test | Next |
| 4 | Community release (docs, repo, packaging) | Future |
