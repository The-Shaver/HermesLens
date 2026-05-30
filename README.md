# HermesLens

**Physical dashboard for Hermes Agent — M5 StickS3 + ESP32-S3**

An open-source, community-friendly dashboard that brings your Hermes Agent setup to life on a compact $20 display. See agents, tasks, system health, and usage stats at a glance.

---

## Current Status

- **Firmware:** C++ / PlatformIO — working captive portal, config persists, boots to dashboard
- **Backend:** Python / FastAPI — runs locally, aggregates Hermes Agent data
- **Flashing:** 3-file flash (bootloader + partitions + firmware) — tested and working
- **Target:** M5 StickS3 (ESP32-S3, 240×135 ST7789 LCD, touch + buttons)

**What works now:**
- M5 boots → captive portal fires if no config saved
- Portal saves WiFi + backend URL → reboots → dashboard loads
- Dashboard polls backend and renders 4 pages (Agents, Tasks, System, Usage)
- Touch swipe + physical button navigation
- Config persists across reboots (NVS storage)

**What still needs work:**
- Backend → M5 data pipeline polish
- End-to-end testing against live Hermes data
- Community docs + README cleanup ← *we are here*

---

## Quick Start

### Prerequisites
- M5 StickS3
- USB-C cable (data-capable)
- Computer with Python 3 + PlatformIO
- 2.4 GHz WiFi network
- Hermes Agent running locally

### 1. Build the Firmware

```bash
cd firmware-cpp
pio run -e hermeslens-s3
```

Output: `firmware-cpp/.pio/build/hermeslens-s3/firmware.bin`

### 2. Flash the M5 StickS3

Flash three files at specific offsets:

| File | Offset |
|------|--------|
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x00010000` |

Use **M5Burner** or a **web flasher** (e.g., https://m5burner.m5stack.com/) and add each file with its offset.

### 3. Start the Backend

```bash
cd backend
pip install -r requirements.txt
python server.py
```

Backend runs at `http://<your-ip>:8123`.

### 4. Configure the M5

1. Power on the M5 — it creates a WiFi AP named **HermesLens-Setup**
2. Connect to that AP from your phone/laptop
3. A captive portal opens (or go to `http://192.168.4.1`)
4. Enter your WiFi SSID, password, and backend URL (e.g. `http://192.168.1.50:8123`)
5. Tap **Save & Reboot**

Dashboard loads on the M5 screen within ~5–10 seconds.

---

## Project Structure

```
hermeslens/
├── README.md              ← You are here
├── DESIGN.md              ← Design decisions and API schema
├── PLAN.md                ← High-level milestones
├── FLASH_INSTRUCTIONS.md  ← Detailed flashing guide
│
├── backend/               ← Python FastAPI data service
│   ├── server.py          ← FastAPI entrypoint, /api/status + /api/health
│   ├── config.py          ← YAML config loader
│   ├── requirements.txt   ← fastapi, uvicorn, pyyaml
│   └── sources/           ← Data collectors
│       ├── gateway.py     ← gateway_state.json
│       ├── sessions.py    ← state.db
│       ├── kanban.py      ← kanban.db
│       ├── profiles.py    ← ~/.hermes/profiles/
│       └── cron.py        ← cron jobs
│
├── firmware-cpp/          ← ESP32-S3 firmware (PlatformIO)
│   ├── platformio.ini     ← Build config (espressif32@6.10.0)
│   ├── src/
│   │   ├── main.cpp       ← Boot, portal, dashboard loop
│   │   ├── config.hpp     ← NVS config storage (Preferences.h)
│   │   ├── display.hpp    ← M5 display wrapper, palette, fonts
│   │   ├── wifi_manager.hpp ← WiFi STA + AP
│   │   ├── api_client.hpp ← HTTP client, ArduinoJson
│   │   ├── setup_portal.hpp ← Captive portal AP + DNS + form
│   │   └── pages.hpp      ← 4 dashboard page renderers
│   ├── include/           ← Shared headers (palette.hpp, font.hpp)
│   └── data/
│       └── setup.html     ← Captive portal HTML form
│
├── docs/
│   └── hardware-setup.md  ← Pointer/redirect to current flashing docs
│
└── examples/
    └── hermeslens.yaml    ← Example backend config
```

---

## Game Plan

| Phase | Status | Focus |
|-------|--------|-------|
| **0 — Design** | ✅ Complete | Decisions locked, docs written |
| **1 — Backend** | 🔜 In progress | Polish collectors, verify /api/status against live Hermes |
| **2 — Firmware** | ✅ Working | C++ rewrite done; portal + dashboard + display verified |
| **3 — Polish** | 🔜 Next | Error screens, loading states, docs cleanup |
| **4 — Release** | ⏳ Future | GitHub repo, community guide, demo media |

**Next milestone:** Get the backend producing clean `/api/status` data for the firmware, then test the full pipeline end-to-end.

## Phases

### Phase 0: Design ✅
- [x] Project named: HermesLens
- [x] Architecture: backend + C++ firmware on M5 StickS3
- [x] API schema and data sources defined

### Phase 1: Backend 🔜 In progress
- [x] FastAPI server with `/api/status` and `/api/health`
- [x] Config loader (YAML)
- [x] Data collectors: gateway, sessions, kanban, profiles, cron
- [x] Graceful defaults when Hermes data is missing

### Phase 2: Firmware ✅
- [x] C++ / PlatformIO rewrite
- [x] WiFi STA + AP captive portal
- [x] NVS config persistence
- [x] 4-page dashboard (Agents, Tasks, System, Usage)
- [x] Touch + physical button navigation

### Phase 3: Polish 🔜 Next
- [ ] Backend schema verified against live Hermes data
- [ ] End-to-end hardware test
- [ ] Error screens + loading states
- [ ] Final docs cleanup

### Phase 4: Release ⏳
- [ ] GitHub repo
- [ ] Community flash package
- [ ] Demo media
---

## Contributing

This is an open-source project. Bugs, features, and docs improvements are welcome.

1. Firmware changes: edit `firmware-cpp/src/`, rebuild with `pio run`
2. Backend changes: edit `backend/sources/`, restart `server.py`
3. Docs: update this README first, then patch legacy files

See `DESIGN.md` for API contracts and data schemas.

---

## License

MIT
