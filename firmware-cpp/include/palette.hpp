/**
 * HermesLens — Display Palette
 * ==============================
 * Shared RGB565 color constants used across all firmware modules.
 */

#ifndef HERMESLENS_PALETTE_HPP
#define HERMESLENS_PALETTE_HPP

// ══ Display dimensions ═══════════════════════════════════════════
static const uint16_t SCREEN_W = 240;
static const uint16_t SCREEN_H = 135;
static const uint8_t  HEADER_H = 20;

// ══ Color palette — RGB565 (16-bit) ══════════════════════════════
// Utility helper — converts 8-bit RGB channels to 16-bit RGB565
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t((r & 0xF8) << 8) |
            uint16_t((g & 0xFC) << 3) |
            uint16_t((b & 0xF8) >> 3));
}

// ── Background / surface ────────────────────────────────────────
static const uint16_t COLOR_BG      = rgb565(26,  26,  46);  // #1A1A2E navy
static const uint16_t COLOR_CARD    = rgb565(22,  33,  62);  // #16213E panel
static const uint16_t COLOR_HEADER  = rgb565(15,  15,  35);  // #0F0F23 dark
static const uint16_t COLOR_OVERLAY = rgb565(10,  10,  18);  // dark overlay

// ── Text ────────────────────────────────────────────────────────
static const uint16_t COLOR_WHITE   = rgb565(255, 255, 255);
static const uint16_t COLOR_GRAY    = rgb565(160, 160, 176);
static const uint16_t COLOR_DIM     = rgb565( 90,  90, 110);  // dimmed text

// ── Status / semantic ───────────────────────────────────────────
static const uint16_t COLOR_GREEN   = rgb565(0,   255, 136);  // #00FF88 online
static const uint16_t COLOR_BLUE    = rgb565(0,   170, 255);  // #00AAFF active
static const uint16_t COLOR_RED     = rgb565(255,   0,   0);
static const uint16_t COLOR_YELLOW  = rgb565(255, 204,   0);
static const uint16_t COLOR_ORANGE  = rgb565(255, 136,   0);

#endif  // HERMESLENS_PALETTE_HPP
