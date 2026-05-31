/**
 * HermesLens — Dashboard Pages (M8)
 * ===================================
 * Sliding-window header-only renderers for the 4-page dashboard.
 * All functions are inline so no .cpp file is needed.
 *
 * Display: 240x135 portrait  |  FONT_W=8  FONT_H=13
 *  y=0..19   header bar (HEADER_H = 20)
 *  y=22..134 content            (113 px usable)
 *  4 bottom-right dots          page presence indicator
 *
 * Pages index matches _diagPage enum (0-3):
 *   DIAG_OVERVIEW (0) = Agents   slide-window over agent list
 *   DIAG_DETAIL   (1) = Tasks    3-tile To-Do|Active|Blocked + Done% row
 *   DIAG_WIFI     (2) = System   4 key:value label rows
 *   DIAG_META     (3) = Usage    model + tokens + cost$
 */

#ifndef HERMESLENS_PAGES_HPP
#define HERMESLENS_PAGES_HPP

#include <Arduino.h>
#include <cstring>
#include "display.hpp"
#include "api_client.hpp"

// Pull in the global display+pages scope declared in main.cpp
extern HermesDisplay display;
extern const uint8_t PAGES;

// ── Layout constants ─────────────────────────────────────────────────
static constexpr uint8_t AGENT_LINE_H  = FONT_H + 2;  // 15 px per agent row
static constexpr uint8_t AGENT_VISIBLE = 7;            // rows visible
static constexpr uint8_t DOT_SLOTS     = 4;            // dot-bar width

// Content starts here
//   y = HEADER_H (20) + preamble line (14) = 34  first row
//   last row = 20 + 14 + (7 * 15) = 139 > 133?
//   Use contentY = HEADER_H + 14 = 34; that's 139 max which doesn't fit.
//   Actually: y0=34, 7 rows * 15 = 105, last row end=34+105=139 -> 139>135.
//   Shrink visible to 6 rows: 34+6*15=124 fits within 133.
static constexpr uint8_t AGENTS_PG_VIS = 6;   // must fit in 240x135

static inline int clampWin(int start, int total, int visible) {
    if (total <= visible) return 0;
    if (start < 0)         return 0;
    if (start > total - visible) return total - visible;
    return start;
}
static inline uint8_t dotsActive(int total, int winStart, int visible) {
    if (total <= 1) return 1;
    return (uint8_t)((winStart + 1) * DOT_SLOTS / total);
}

// ── Agents page ───────────────────────────────────────────────────────

/** Render the agents sliding-window page.
 *  @param agents    agent list from API
 *  @param pageId    tab index (0 = Agents)
 *  @param isActive  true when Agents is the visible/selected tab
 *  @param winStart  first visible row index, managed by main.cpp loop
 */
static inline void renderAgentsPage(const AgentsData& agents,
                                    uint8_t pageId, bool isActive, int winStart)
{
    // Clear only the content area, not the full 240×135 frame
    display.fillRect(0, HEADER_H + 2, SCREEN_W, SCREEN_H - HEADER_H - FONT_H - 2, COLOR_BG);
    int total = (int)agents.agents.size();
    int first = clampWin(winStart, total, AGENTS_PG_VIS);

    uint16_t hdrCol = isActive ? COLOR_WHITE : COLOR_DIM;
    display.drawHeader(isActive ? "Agents" : "Agents  --");
    display.drawPageIndicator(pageId, PAGES);

    // Preamble: total count
    {
        char buf[20];
        snprintf(buf, sizeof(buf), "%d agent%s", total, total == 1 ? "" : "s");
        display.drawText(4, HEADER_H + 2, buf, COLOR_DIM);
    }

    int y = HEADER_H + 14;
    for (int i = 0; i < AGENTS_PG_VIS && first + i < total; i++) {
        const auto& a = agents.agents[first + i];
        // Status dot
        bool busy = a.status.equals("active") || a.status.equals("busy")
                    || a.status.equals("running");
        display.fillRect(4, y + 3, 5, 5, busy ? COLOR_GREEN : COLOR_GRAY);

        // Name
        char tag[16];
        strncpy(tag, a.name.c_str(), sizeof(tag) - 1);
        tag[sizeof(tag) - 1] = '\0';
        display.drawText(12, y, tag, hdrCol);

        // Role right of name, only if it fully fits before right margin
        int nx = 12 + (int)strlen(tag) * FONT_W + 2;
        int roleW = display.textWidth(a.role.c_str());
        if (nx + roleW < SCREEN_W - 4)
            display.drawText(nx, y, a.role.c_str(), COLOR_ORANGE);

        y += AGENT_LINE_H;
    }

    // Dot bar: right-bottom, 4 dots
    int dOn = dotsActive(total, first, AGENTS_PG_VIS);
    for (int d = 0; d < DOT_SLOTS; d++) {
        uint16_t dc = d < dOn ? COLOR_BLUE : COLOR_DIM;
        display.fillRect(SCREEN_W - 3 - (DOT_SLOTS - d) * 6,
                          SCREEN_H - FONT_H - 2, 4, 4, dc);
    }
}

// ── Tasks page ─────────────────────────────────────────────────────────

/**
 * Tasks summary: four tiles — two rows
 *  row-0: To-Do | Active | Blocked
 *  row-1: Done% full-width
 */
static inline void renderTasksPage(const TasksData& tasks,
                                    uint8_t pageId, bool isActive)
{
    display.fillRect(0, HEADER_H + 2, SCREEN_W, SCREEN_H - HEADER_H - FONT_H - 2, COLOR_BG);
    int todo   = tasks.ready + tasks.blocked;
    int active = tasks.in_progress;
    int doneN  = tasks.done;
    int total  = todo + active + doneN;

    uint16_t hdrCol = isActive ? COLOR_WHITE : COLOR_DIM;
    display.drawHeader(isActive ? "Tasks" : "Tasks  --");
    display.drawPageIndicator(pageId, PAGES);

    // Total count preamble
    { char buf[20]; snprintf(buf, sizeof(buf), "%d total", total);
      display.drawText(4, HEADER_H + 2, buf, COLOR_DIM); }

    constexpr uint8_t TW = 62;        // tile inner width
    constexpr uint8_t G  =  7;        // tile gap
    constexpr uint8_t TH = 30;        // tile inner height
    const uint8_t by0   = HEADER_H + 22;  // tile row 0 top

    auto tile = [&](uint8_t i, const char* label, uint16_t labelC,
                    int value, uint16_t valueC, uint16_t borderC) {
        uint8_t bx = 4 + i * (TW + G);
        display.fillRect(bx, by0, TW, TH + 6, COLOR_CARD);
        display.drawRect(bx, by0, TW, TH + 6, borderC);
        display.drawTextCenteredAt(bx + 3, by0 + 3, bx + TW - 3, label, labelC);
        char vb[6]; snprintf(vb, sizeof(vb), "%d", value);
        uint16_t vw = strlen(vb) * FONT_W;
        display.drawText(bx + 3 + (TW - vw) / 2, by0 + 17, vb, valueC);
    };

    tile(0, "To do",   COLOR_DIM,        todo,   COLOR_YELLOW, COLOR_GRAY);
    tile(1, "Active",  COLOR_DIM,        active, COLOR_BLUE,   COLOR_GRAY);
    tile(2, "Blocked", COLOR_DIM,        tasks.blocked, COLOR_RED, COLOR_GRAY);

    // Done% — full-width row
    {
        int doneY = by0 + TH + G + 2;
        display.fillRect(4, doneY, SCREEN_W - 8, TH, COLOR_CARD);
        display.drawRect(4, doneY, SCREEN_W - 8, TH, COLOR_GREEN);
        double pct = total > 0 ? (double)doneN / total * 100.0 : 0.0;
        char buf[32];
        snprintf(buf, sizeof(buf), "Done  %.1f%%  %d / %d", pct, doneN, total);
        uint16_t dw = strlen(buf) * FONT_W;
        display.drawText((int)(SCREEN_W - dw) / 2, doneY + 8, buf, COLOR_WHITE);
    }
}

// ── System page ────────────────────────────────────────────────────────

/** System status: 4 key:value lines. */
static inline void renderSystemPage(const ApiStatus& status,
                                     const HermesConfig& cfg,
                                     uint8_t pageId, bool isActive)
{
    display.fillRect(0, HEADER_H + 4, SCREEN_W, SCREEN_H - HEADER_H - FONT_H - 2, COLOR_BG);
    uint16_t hdrCol = isActive ? COLOR_WHITE : COLOR_DIM;
    display.drawHeader(isActive ? "System" : "System  --");
    display.drawPageIndicator(pageId, PAGES);

    struct Row { const char* label; const char* val; };
    Row rows[] = {
        { "Hermes",  status.hermes_version.c_str()  },
        { "Gateway", status.gateway_state.c_str()    },
        { "Poll",    "10 s"                           },
        { "Backend", cfg.backend_url.c_str()          },
    };

    int y = HEADER_H + 4;
    for (auto &r : rows) {
        display.drawText(4, y, r.label, COLOR_DIM);
        int tw = display.textWidth(r.val);
        int vx = SCREEN_W - tw - 6;
        if (vx < 70) vx = 70;
        display.drawText(vx, y, r.val, hdrCol);
        y += 14;
        if (y > (int)SCREEN_H - FONT_H - 2) break;
    }
}

// ── Usage page ─────────────────────────────────────────────────────────

/** Usage stats: model, tokens, calls, cost. */
static inline void renderUsagePage(const ApiStatus& status,
                                    uint8_t pageId, bool isActive)
{
    display.fillRect(0, HEADER_H + 2, SCREEN_W, SCREEN_H - HEADER_H - FONT_H - 2, COLOR_BG);
    uint16_t hdrCol = isActive ? COLOR_WHITE : COLOR_DIM;
    display.drawHeader(isActive ? "Usage" : "Usage  --");
    display.drawPageIndicator(pageId, PAGES);

    // Sub-header: current model
    {
        const char* lbl = "Model:";
        display.drawText(4, HEADER_H + 2, lbl, COLOR_DIM);
        const char* model = status.current_model.length() > 0
                            ? status.current_model.c_str() : "--";
        display.drawText(4 + FONT_W * 6, HEADER_H + 2, model, COLOR_WHITE);
    }

    struct { const char* label; unsigned long val; } statRows[] = {
        { "Input",    status.sessions.tokens_input      },
        { "Output",   status.sessions.tokens_output     },
        { "API calls",status.sessions.tool_calls_total  },
    };

    int y = HEADER_H + 18;
    for (auto &s : statRows) {
        display.drawText(4, y, s.label, COLOR_DIM);
        char sb[16];
        snprintf(sb, sizeof(sb), "%lu", s.val);
        int tw = display.textWidth(sb);
        int vx = SCREEN_W - tw - 6;
        if (vx < 72) vx = 72;
        display.drawText(vx, y, sb, COLOR_WHITE);
        y += 14;
        if (y > (int)SCREEN_H - FONT_H - 4) break;
    }

    // Session cost row
    {
        float sc = status.sessions.session_cost_usd;
        char scStr[16];
        snprintf(scStr, sizeof(scStr), "%.2f", isnan(sc) ? 0.0f : sc);
        display.drawText(4, y, "Session", COLOR_DIM);
        int tw = display.textWidth(scStr);
        int vx = SCREEN_W - tw - 6;
        if (vx < 72) vx = 72;
        display.drawText(vx, y, scStr, COLOR_WHITE);
        y += 14;
    }

    // Cost bar at bottom
    if (y + FONT_H + 4 < (int)SCREEN_H) {
        int bx = 4, bw = SCREEN_W - 8, by = y + 2;
        display.fillRect(bx, by, bw, FONT_H + 4, COLOR_CARD);
        display.drawRect(bx, by, bw, FONT_H + 4, COLOR_GRAY);
        display.drawText(8, by + 2, "Overall $", COLOR_DIM);
        char cb[16];
        float cost = status.sessions.estimated_cost_usd;
        snprintf(cb, sizeof(cb), "%.2f", isnan(cost) ? 0.0f : cost);
        uint16_t cw = strlen(cb) * FONT_W;
        display.drawText(bx + bw - cw - 4, by + 2, cb, COLOR_WHITE);
    }
}

#endif  /* HERMESLENS_PAGES_HPP */
