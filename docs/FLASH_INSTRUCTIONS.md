# HermesLens Firmware — Flash Instructions

**M5 StickS3 | Uses default partition table. No erase-all required.**

This guide is written for non-technical users. Follow the method that matches your OS and comfort level.

---

## What you need

- M5 StickS3
- USB-C cable
- Windows, macOS, or Linux
- The **3 firmware files** from this repo:
  - `bootloader.bin`
  - `partitions.bin`
  - `firmware.bin`

---

## Important: offsets matter

| File | Offset |
|------|--------|
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x00010000` |

If you use Web Flasher or M5Burner desktop app, these offsets are entered into the UI. If you use `esptool`, they are used on the command line.

---

## Method A: M5Burner Web (Recommended)

1. Open this URL in your browser:
   - https://esptool.spacehuhn.com/
2. Add the 3 files with offsets shown above:
   - Bootloader → `0x0000`
   - Partitions → `0x8000`
   - Firmware → `0x00010000`
3. Click **Burn**
4. When the status bar says done, power-cycle the device

Notes:
- The web flasher uses WebUSB. In some browsers you must allow the browser to access the USB device.
- If your OS shows a new serial port after flashing, that’s normal.

---

## Method B: M5Burner Desktop (Windows/macOS/Linux)

1. Download and install M5Burner from https://docs.m5stack.com/en/download
2. Connect the M5 StickS3 via USB-C
3. Open M5Burner
4. Select **M5StickS3**
5. Click **Erase**
   - Use **Erase**, not **Erase All**
6. Click the **+** button and add the 3 files with offsets:
   - `bootloader.bin` → `0x0000`
   - `partitions.bin` → `0x8000`
   - `firmware.bin` → `0x00010000`
7. Click **Burn**
8. When complete, power-cycle the device

---

## Method C: esptool (advanced)

Prerequisites: Python and `esptool` installed.

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0000 bootloader.bin \
             0x8000 partitions.bin \
             0x00010000 firmware.bin
```

Replace `/dev/ttyACM0` with your port:
- Linux: `/dev/ttyUSB0`, `/dev/ttyACM0`
- macOS: `/dev/cu.usbserial-...`
- Windows: `COM3`, `COM4`, etc.

---

## First Boot

1. Power on the M5 StickS3
2. Wait ~3-5 seconds
3. The screen shows **HermesLens Setup**
4. Your phone/laptop sees WiFi network **HermesLens-Setup** (open, no password)
5. Connect to it
6. A captive portal opens automatically, or open a browser and go to:
   - `http://192.168.4.1`
7. Fill in:
   - **WiFi SSID** — your 2.4 GHz WiFi network name
   - **WiFi Password** — your WiFi password
   - **Backend URL** — e.g. `http://192.168.1.50:8123`
8. Tap **Save & Reboot**
9. The device reboots, connects to your WiFi, and shows the dashboard

If it stays on **Connecting...** for more than ~15 seconds:
- Re-enter the backend URL
- Confirm the backend is reachable from the same WiFi
- Turn off AP/client isolation on your router if enabled

---

## Reset Config

- While in setup mode: visit `http://192.168.4.1/wipe`
- Or re-flash the 3 files; this clears NVS and the portal starts again automatically

---

## Serial Debug

Connect at **115200 baud** to the device serial port to see boot logs:
- `[main] Starting setup portal`
- `[portal] save-verify: load=OK ssid='...' backend='...'`
- `[api] GET http://.../api/status`
- `[api] HTTP code=200`

---

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Portal opens but Save fails | Backend unreachable from M5 WiFi |
| Screen stays black | Backlight/power issue or wrong firmware |
| Connects to WiFi, then portal again after reboot | Config not saved (reflash with correct offsets and try again) |
| Browser never shows portal | Manually visit `http://192.168.4.1` |

---

## Files

| File | Size |
|------|------|
| `firmware.bin` | ~1,073 KB |
| `bootloader.bin` | ~18 KB |
| `partitions.bin` | ~3 KB |

---

*Community project. If you get stuck, open an issue on GitHub.*
