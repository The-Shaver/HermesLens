# HermesLens Firmware — Flash Instructions

**M5 StickS3 | ESP32-S3 | No custom partition table needed.**

This firmware uses the ESP32-S3 default partition table (NVS at 0x9000).
Flash requires **3 separate files** at specific offsets.

---

## Required Files

| File | Offset | Size |
|------|--------|------|
| `bootloader.bin` | `0x0000` | ~15 KB |
| `partitions.bin` | `0x8000` | ~3 KB |
| `firmware.bin` | `0x00010000` | ~1,073 KB |

**All three files are in this folder.**

---

## Flash Methods

### Method A: Web Flasher (Recommended for non-technical users)

1. Open a web flasher in your browser:
   - https://m5burner.m5stack.com/
   - OR https://esp.huhn.me/
2. Select board: **ESP32S3 Dev Module** or **M5StickS3**
3. Add the 3 files with their offsets:

| File | Offset |
|------|--------|
| bootloader.bin | 0x0000 |
| partitions.bin | 0x8000 |
| firmware.bin | 0x00010000 |

4. Click **Flash**
5. Done.

### Method B: M5Burner (Desktop)

1. Connect M5 StickS3 via USB-C
2. Open M5Burner → select **M5StickS3**
3. Click **Erase** (NOT "Erase All")
4. In the Burn dialog, add the 3 files with offsets shown above
5. Click Burn
6. Done.

### Method C: esptool (advanced)

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0000 bootloader.bin \
             0x8000 partitions.bin \
             0x00010000 firmware.bin
```

---

## First Boot

1. Power on the device
2. Wait ~3-5 seconds
3. The M5 screen shows **"HermesLens Setup"** with AP name and IP
4. Your computer/phone sees **"HermesLens-Setup"** WiFi network
5. Connect to it (no password)
6. A captive portal opens automatically, OR open browser and go to **`http://192.168.4.1`**
7. Fill in:
   - **WiFi SSID** — your 2.4 GHz WiFi network name
   - **WiFi Password** — your WiFi password
   - **Backend URL** — e.g. `http://192.168.1.50:8123`
8. Tap **"Save & Reboot"**
9. The device reboots and connects to your WiFi
10. Dashboard loads on the M5 screen

---

## Wipe Config

If you need to reset:
- While in portal: visit `http://192.168.4.1/wipe`
- Or re-flash all 3 files (NVS keys will be empty, portal fires automatically)

---

## Serial Monitor

Connect at 115200 baud to `/dev/ttyACM0` (or your port) to see boot logs.

---

## Files

- `firmware.bin` — application (1,073 KB)
- `bootloader.bin` — ESP32-S3 bootloader (15 KB)
- `partitions.bin` — partition table (3 KB)

---

*Community project. Done.*
