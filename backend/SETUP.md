# HermesLens Backend — Setup Guide

The backend is a small FastAPI service that reads live Hermes Agent data and serves it to the M5 StickS3.

## Requirements

- Python 3.9+
- Hermes Agent installed with data in `~/.hermes/` (or set `HERMES_HOME`)
- Network path from the M5 StickS3 to this machine

## Install

```bash
cd backend
python -m venv .venv
source .venv/bin/activate  # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

## Run

```bash
python server.py
```

It listens on `http://0.0.0.0:8123` by default.

## Configuration

The backend reads config from three sources, merged in this order:
1. Built-in defaults
2. YAML config file (if `HERMESLENS_CONFIG` is set)
3. Environment variables

### YAML config

Set `HERMESLENS_CONFIG` to a config file path:

```yaml
port: 8123
host: 0.0.0.0
refresh_interval: 10
pages:
  - agents
  - tasks
  - system
  - usage
api_key: null
```

### Environment variables

| Variable | Description | Default |
|----------|-------------|---------|
| `PORT` | Server port | `8123` |
| `HOST` | Bind address | `0.0.0.0` |
| `HERMESLENS_API_KEY` | Optional API key for `/api/status` | `None` |
| `HERMES_HOME` | Path to Hermes home directory | `~/.hermes` |

### Hermes data

The backend reads from:
- `~/.hermes/gateway_state.json`
- `~/.hermes/state.db`
- `~/.hermes/kanban.db`
- `~/.hermes/profiles/`

It never writes to `~/.hermes`.

## API

### Health check

```bash
curl http://localhost:8123/api/health
```

Response:

```json
{"status":"ok","version":"0.1.0"}
```

### Dashboard data

```bash
curl http://localhost:8123/api/status
```

If `HERMESLENS_API_KEY` is set, include the header:

```bash
curl -H "X-API-Key: your-key" http://localhost:8123/api/status
```

The response contains:
- `gateway` — platform connections, active agent count
- `sessions` — session counts, token counts, cost estimates
- `tasks` — kanban-ready/in-progress/blocked/done counts, recent tasks
- `agents` — names, roles, status, current task
- `current_model` — most recent session model
- `session_cost_usd` — current session cost

If a source is missing, the backend returns safe zero values. It never returns a 500 to the M5 StickS3.

## Notes

- The M5 must be able to reach this address on your network. Use a LAN IP, not `localhost` or `127.0.0.1`, when configuring the device.
- The server is read-only with respect to Hermes data.
- Logs are printed to stdout with `[LEVEL]` prefixes.
