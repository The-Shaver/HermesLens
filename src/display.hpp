/**
 * HermesLens — Display
 * ====================
 * Wrapper around M5.Display (M5Unified / LovyanGFX) for the
 * 240×135 ST7789 on the M5 StickS3.
 *
 * Provides:
 *  - TinyUF2 8×13 bitmap font rendering
 *  - Color palette constants (from palette.hpp)
 *  - High-level UI helpers: header, card, progress bar, status dot
 *  - Full-screen splash screen
 *
 * Known: The M5StickS3 ST7789 LCD cfg is portrait 240×135 (auto-detected
 *        by M5Unified on M5.begin()).
 *        drawChar right-edge guard at _w - FONT_W prevents pixel overrun in
 *        CASET/RASET window registers.
 */

#ifndef HERMESLENS_DISPLAY_HPP
#define HERMESLENS_DISPLAY_HPP

#include <M5Unified.h>
#include <Arduino.h>       // for String, Serial
#include "palette.hpp"
#include "font.hpp"
#include <cstdint>

class HermesDisplay {
public:
    HermesDisplay() = default;

    bool init() {
        auto cfg = M5.config();
        cfg.clear_display        = true;
        cfg.internal_imu         = false;  // we don't need the IMU
        cfg.internal_rtc         = false;
        cfg.internal_spk         = false;  // no speaker
        cfg.internal_mic         = false;
        cfg.output_power         = false;
        cfg.led_brightness       = 0;

        M5.begin(cfg);
        _display = &M5.Display;

        _w  = SCREEN_W;
        _h  = SCREEN_H;
        _fg = COLOR_WHITE;
        _bg = COLOR_BG;
        return true;
    }

    void fillScreen(uint16_t color) { _display->fillScreen(color); }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        _display->fillRect(x, y, w, h, color);
    }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        _display->drawRect(x, y, w, h, color);
    }
    void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        _display->drawFastHLine(x, y, w, color);
    }
    void drawPixel(int16_t x, int16_t y, uint16_t color) {
        _display->drawPixel(x, y, color);
    }
    uint16_t color(uint8_t r, uint8_t g, uint8_t b) const {
        return rgb565(r, g, b);
    }

    // ── Boot / splash ──────────────────────────────────────────

    void splash(const char* title, const char* subtitle) {
        fillScreen(COLOR_BG);
        drawTextCentered(_h / 2 - 12, title, COLOR_WHITE);
        if (subtitle) {
            drawTextCentered(_h / 2 + 4, subtitle, COLOR_GRAY);
        }
    }

    // ── Offline / error helpers ─────────────────────────────────

    void showOffline(const char* ssid) {
        fillScreen(COLOR_BG);
        drawTextCentered(60,  "No WiFi",         COLOR_WHITE);
        drawTextCentered(76,  "Check network",   COLOR_GRAY);
        if (ssid && strlen(ssid) > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "SSID: %s", ssid);
            drawTextCentered(94, buf, COLOR_GRAY);
        }
    }

    void showBackendOffline(const String& backendUrl) {
        fillScreen(COLOR_BG);
        drawTextCentered(56,  "Backend Offline",     COLOR_WHITE);
        drawTextCentered(72,  "Check HermesLens",    COLOR_GRAY);
        if (backendUrl.length() > 0) {
            drawTextCentered(90,  backendUrl.c_str(), COLOR_GRAY);
        }
    }

    void showSetupMode() {
        fillScreen(COLOR_BG);
        // 135px tall screen, header at y=0..20.
        // Keep all lines within 235px safe width (240-5 margin).
        // Left-align with 4px indent, right-edge guard in drawChar.
        int16_t lx = 4;  // left indent
        drawText(lx, 28, "Setup Mode",            COLOR_WHITE);
        drawText(lx, 48, "Connect to:",            COLOR_GRAY);
        drawText(lx, 68, "HermesLens-Setup",       COLOR_GREEN);
        drawText(lx, 88, "Then open browser at",   COLOR_GRAY);
        drawText(lx, 108, "192.168.4.1",            COLOR_GRAY);
        drawText(lx, 128, "WiFi + backend",         COLOR_WHITE);
    }

    void showConfigSaved() {
        fillScreen(COLOR_BG);
        drawTextCentered(_h / 2 - 12, "Config Saved!", COLOR_GREEN);
        drawTextCentered(_h / 2 + 4,  "Rebooting...",  COLOR_GRAY);
    }

    // ── Header ─────────────────────────────────────────────────

    // ── Setup portal diagnostic ────────────────────────────────

    /**
     * Shows why setup portal was triggered (needsSetup diagnostics).
     * Displays 4 lines: trigger reason + ssid + backend url + empty-fields list.
     * Called at the top of runPortal() so the operator can read it
     * without needing a serial console.
     */
    void showSetupDiagnostic(const char* triggerReason,
                             const char* ssid,
                             const char* backend,
                             const char* missingFields) {
        fillScreen(COLOR_BG);
        // L0 — trigger
        drawTextCentered(20, triggerReason, COLOR_RED);
        // separator
        for (int x = 0; x < _w; x += 2) drawPixel(x, 30, COLOR_GRAY);
        // L1 — wifi
        drawTextCentered(40, ssid[0] ? ssid : "(not set)", ssid[0] ? COLOR_GRAY : COLOR_YELLOW);
        // L2 — backend
        drawTextCentered(54, backend[0] ? backend : "(not set)", backend[0] ? COLOR_GRAY : COLOR_YELLOW);
        // L3 — empty fields
        drawTextCentered(68, "Empty:", COLOR_RED);
        drawTextCentered(80, missingFields[0] ? missingFields : "none", COLOR_YELLOW);
        // L4 — still enter portal
        drawTextCentered(100, "Starting portal...", COLOR_GREEN);
    }

    void drawHeader(const char* title) {
        fillRect(0, 0, _w, HEADER_H, COLOR_HEADER);
        if (_wifiConnected) {
            fillRect(_w - 12, 4, 8, 8, COLOR_GREEN);
        }
        // Title — center, left-clamp so we never miss the title
        uint16_t tw = textWidth(title);
        int16_t tx = (_w - (int16_t)tw) / 2;
        if (tx < 2) tx = 2;
        drawText(tx, 4, title, COLOR_WHITE);
        drawHLine(0, HEADER_H - 1, _w, COLOR_GRAY);
    }

    void drawPageIndicator(uint8_t current, uint8_t total) {
        char buf[8];
        snprintf(buf, sizeof(buf), "[%d/%d]", current + 1, total);
        uint16_t tw = textWidth(buf);
        drawText(_w - tw - 2, _h - FONT_H - 2, buf, COLOR_GRAY);
    }

    // ── Card / progress ────────────────────────────────────────

    void drawCard(int16_t x, int16_t y, int16_t w, int16_t h) {
        fillRect(x, y, w, h, COLOR_CARD);
        drawRect(x, y, w, h, COLOR_GRAY);
    }

    void drawProgressBar(int16_t x, int16_t y, int16_t w, uint8_t pct) {
        uint16_t h = 6;
        drawRect(x, y, w, h, COLOR_GRAY);
        int16_t fw = (int16_t)((int32_t)w * pct / 100);
        if (fw > 2) {
            fillRect(x + 1, y + 1, fw - 2, h - 2, COLOR_BLUE);
        }
    }

    void drawStatusDot(int16_t x, int16_t y, bool active) {
        uint16_t c = active ? COLOR_GREEN : COLOR_GRAY;
        fillRect(x - 3, y - 3, 7, 7, c);
    }

    // ── Text rendering ─────────────────────────────────────────

    void drawText(int16_t x, int16_t y, const char* str, uint16_t color) {
        uint16_t fg = _fg;
        _fg = color;
        for (const char* p = str; *p; ++p) {
            drawChar(x, y, *p);
            x += FONT_W;
        }
        _fg = fg;
    }

    void drawTextCentered(int16_t y, const char* str, uint16_t color) {
        uint16_t tw = textWidth(str);
        int16_t x = (_w - (int16_t)tw) / 2;
        if (x < 2) x = 2;
        drawText(x, y, str, color);
    }

    void drawTextRight(int16_t y, const String& str, uint16_t color) {
        uint16_t tw = textWidth(str.c_str());
        int16_t x = _w - (int16_t)tw - 2;
        drawText(x, y, str.c_str(), color);
    }

    void drawChar(int16_t x, int16_t y, char c) {
        // right-edge guard: never place a character past the right boundary
        // This prevents CASET/RASET overrun on ST7789 column-end register.
        if (x > (int16_t)(_w - FONT_W))  x = _w - FONT_W;
        if (x < 0)                        x = 0;
        if (y > (int16_t)(_h - FONT_H))  y = _h - FONT_H;
        if (y < 0)                        y = 0;

        if (c < 32 || c > 126) c = ' ';
        uint16_t idx = (uint16_t)(c - 32) * FONT_BYTES;
        const uint8_t* data = &TINYUF2[idx];
        for (int row = 0; row < FONT_H && idx + row < 1235; ++row) {
            uint8_t byte = data[row];
            for (int col = 0; col < FONT_W; ++col) {
                if (byte & (0x80 >> col)) {
                    _display->drawPixel(x + col, y + row, _fg);
                }
            }
        }
    }

    // ── WiFi status ────────────────────────────────────────────

    void setWifiStatus(bool connected) { _wifiConnected = connected; }
    bool wifiConnected() const         { return _wifiConnected; }

private:
    // Approx pixel width of ASCII string at 8px per char
    uint16_t textWidth(const char* str) const {
        return strlen(str) * FONT_W;
    }
    uint16_t textWidth(const String& s) const {
        return s.length() * FONT_W;
    }

    m5gfx::M5GFX* _display;
    int16_t  _w  = SCREEN_W;
    int16_t  _h  = SCREEN_H;
    uint16_t _fg = COLOR_WHITE;
    uint16_t _bg = COLOR_BG;
    bool     _wifiConnected = false;
};

#endif  // HERMESLENS_DISPLAY_HPP
