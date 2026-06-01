# HermesLens

**Compact dashboard for your Hermes Agent — M5 StickS3**

A small physical display that sits on your desk and shows what your agents are doing in real time. No need to open a dashboard app or check logs — just glance at the M5 StickS3 to see agent status, active tasks, system health, and usage cost.

---

## Current Status

- **Firmware:** C++ / PlatformIO — captive portal saves WiFi + backend URL, persists config in NVS, boots to dashboard
- **Backend:** Python / FastAPI — reads live Hermes data, exposes `/api/health` and `/api/status`
- **Display:** 4 polished pages — Agents, Tasks, System, Usage
- **Connectivity:** M5 polls backend every ~10 seconds and renders live data
- **Hardware:** M5 StickS3 (240×135 display, side/front buttons)

**Out of the box:**
- WiFi setup via captive portal — saves and reboots automatically
- Live dashboard: agents, tasks, system, usage
- Navigation with the side and front buttons

---

## Quick Start

### Prerequisites

- M5 StickS3
- USB-C cable (data-capable)
- Computer with a modern browser (for flashing)
- 2.4 GHz WiFi network
- Hermes Agent installed, or a compatible HermesLens backend

### 1. Flash the Firmware

Flash **3 files** at specific offsets. Detailed instructions: see [FLASH_INSTRUCTIONS.md](FLASH_INSTRUCTIONS.md).

Recommended for most users: **Web Flasher** at https://esptool.spacehuhn.com/
- Add the 3 files with offsets:
  - `bootloader.bin` → `0x0000`
  - `partitions.bin` → `0x8000`
  - `firmware.bin` → `0x00010000`
- Click **Burn**

Alternative: M5Burner Desktop or `esptool`. See `FLASH_INSTRUCTIONS.md` for step-by-step.

### 2. Start the Backend

The backend reads Hermes data and serves it to the M5.

```bash
cd backend
python -m venv .venv
source .venv/bin/activate  # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python server.py
```

It listens on `http://0.0.0.0:8123` by default. Use `python server.py --help` or set `PORT` / `HOST` env vars if needed.

> **Note:** The M5 must be able to reach this address on your network. Use a LAN IP, not `localhost` or `127.0.0.1`.

### 3. Configure the M5

1. Power on the M5 StickS3
2. After ~3–5 seconds it creates WiFi AP **HermesLens-Setup**
3. Connect your phone/laptop to that open network
4. A captive portal opens automatically, or visit `http://192.168.4.1`
5. Enter:
   - **WiFi SSID** — your 2.4 GHz WiFi
   - **WiFi Password** — your WiFi password
   - **Backend URL** — e.g. `http://192.168.1.50:8123`
6. Tap **Save & Reboot**
7. Dashboard loads on the M5 screen within a few seconds

If it stays on **Connecting...** for more than ~15 seconds:
- Re-enter the backend URL
- Confirm the backend is reachable from the same WiFi network
- Disable AP/client isolation on your router

---

## Project Structure

```
hermeslens/
├── README.md              ← You are here
├── DESIGN.md              ← Design decisions and API contract
├── FLASH_INSTRUCTIONS.md  ← Detailed flashing guide
│
├── backend/               ← Python FastAPI data service
│   ├── server.py          ← FastAPI app; /api/health and /api/status
│   ├── config.py          ← Config loader with validation
│   ├── requirements.txt   ← fastapi, uvicorn, pyyaml
│   └── sources/           ← Data collectors
│       ├── gateway.py     ← gateway_state.json
│       ├── sessions.py    ← state.db
│       ├── kanban.py      ← kanban.db
│       └── profiles.py    ← ~/.hermes/profiles/
│
└── firmware-cpp/          ← PlatformIO firmware
    ├── platformio.ini
    └── src/
        ├── config.hpp     ← NVS storage via Preferences.h
        ├── display.hpp    ← Page renderers, palette, fonts
        ├── setup_portal.hpp ← Captive AP + form save
        ├── wifi_manager.hpp
        ├── api_client.hpp
        └── pages.hpp
```

---

## Known Limitations

- Flash uses **3 files**; a single merged binary is planned but not required
- Hermes data is read-only; HermesLens never modifies `~/.hermes`
- Backend expects Hermes data files in `~/.hermes/` (`gateway_state.json`, `state.db`, `kanban.db`)
- Best-effort error handling: if a source is missing, the backend returns safe defaults
- M5 StickS3 needs 2.4 GHz WiFi; 5 GHz is not supported

---

## Contributing

Community contributions are welcome.

1. Read [DESIGN.md](DESIGN.md) for the data contract and architecture
2. Firmware changes live in `firmware-cpp/src/`; rebuild with `pio run -e hermeslens-s3`
3. Backend changes live in `backend/sources/`; restart `python server.py`
4. Issues and PRs welcome — please include reproduction steps and serial/log output when reporting firmware problems

---

## License

MIT
