# HermesLens — Firmware Plan

## Goal

A working, open-source PlatformIO C++ project that anyone can clone, customize, and flash to their M5 StickS3.

## Target Hardware

- M5 StickS3 (ESP32-S3FN8, 8 MB flash, 8 MB Octal PSRAM, 240×135 ST7789 LCD)
- Auto-detected by M5Unified library — no board-specific defines needed

## Architecture

### Dependencies

| Library | Purpose |
|---------|---------|
| `m5stack/M5Unified` | Hardware abstraction — auto-detects M5StickS3, LCD, buttons |
| `m5stack/m5gfx` | Low-level graphics controller for the ST7789 |
| `bblanchon/ArduinoJson` | JSON parsing of `/api/status` response |
| `arduino-libraries/ArduinoHttpClient` | HTTP client for backend polling |

### Modules

- `main.cpp` — Boot splash → setup portal or dashboard → main loop
- `config.hpp` — NVS config storage (WiFi + backend URL)
- `display.hpp` — M5Unified LCD wrapper, palette, fonts, page renderers
- `wifi_manager.hpp` — WiFi STA connect/reconnect, AP mode for setup
- `api_client.hpp` — HTTP GET `/api/health` and `/api/status`, ArduinoJson parsing
- `setup_portal.hpp` — Captive AP + DNS + HTTP + HTML form
- `pages.hpp` — AgentsPage, TasksPage, SystemPage, UsagePage

### Data Flow

```
boot()
  └─→ config.begin() — NVS/Preferences.h
       ├─→ needsSetup() → TRUE  → run_setup_portal()
       │                           AP → 192.168.4.1 → POST /save → NVS
       │                           ← reboot →
       └─→ needsSetup() → FALSE → init_wifi() → dashboard loop
                                    while(true):
                                      api.fetch_status()
                                      pages[current].render(data)
                                      handle_buttons()
```

### Backend API Contract

- `GET /api/health` → `{"status":"ok","version":"0.1.0"}`
- `GET /api/status` → full dashboard JSON (see DESIGN.md)

### Flash Layout

| Offset | File | Purpose |
|--------|------|---------|
| 0x0000 | bootloader.bin | ESP32-S3 bootloader |
| 0x8000 | partitions.bin | Partition table |
| 0x00010000 | firmware.bin | Application |

## Build

```bash
cd firmware-cpp
pio run -e hermeslens-s3
```

Build outputs go to `.pio/build/hermeslens-s3/`.

Run serial monitor:
```bash
pio device monitor -e hermeslens-s3
```

## Notes

- NVS storage replaces the earlier SPIFFS/LittleFS approach
- Captive portal uses async webserver with regex routing
- Button GPIOs: Front=GPIO11, Side=GPIO12
- Display is 240×135 pixels
- NVS handles all config persistence — no partition table edits needed
