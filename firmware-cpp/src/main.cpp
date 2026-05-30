/**
 * HermesLens — Main
 * =================
 * ESP32-S3 firmware for M5 StickS3 physical dashboard.
 *
 * Config: ESP32 NVS (Preferences.h) — no SPIFFS, no custom partition table.
 * Flash: one .bin file, works with M5Burner or any web flasher.
 *
 * Button mapping (M5StickS3):
 *   Front button (GPIO11, M5 logo):
 *     short press → next page
 *     long  press → agent detail (on Agents page)
 *   Side button (right, GPIO12):
 *     short press → prev page
 *     long  press → home (page 0)
 */

#include <Arduino.h>
// NOTE: disableCore0WDT()/enableCore0WDT() are used instead of direct TIMG register
// writes.  TIMERG1 struct is not available at the expected base address on ESP32-S3
// with this SDK version (framework-arduinoespressif32 3.20017.241212).
#include "config.hpp"
#include "display.hpp"
#include "wifi_manager.hpp"
#include "api_client.hpp"
#include "pages.hpp"

// No SPIFFS needed — NVS (Preferences.h) handles config storage.
// NVS uses the standard partition already present in every ESP32-S3 flash.
// No custom partition table, no format-on-boot, no erase hacks.

// ── Boot timing ────────────────────────────────────────────────
static const uint32_t SPLASH_MS   = 2000;
static const uint32_t POLL_MS     = 10000;   // poll every 10 s
static const uint32_t BTN_DEBOUNCE = 50;

// ── State ─────────────────────────────────────────────────────
HermesConfig  cfg;
ConfigManager configMgr;
HermesDisplay display;
WiFiManager   wifi;
ApiClient     api;
ApiStatus     lastStatus;

enum BootPhase {
    PHASE_BOOT,
    PHASE_SETUP_PORTAL,
    PHASE_WIFI,
    PHASE_HEALTH_CHECK,
    PHASE_DASHBOARD,
};
static BootPhase phase = PHASE_BOOT;

uint8_t currentPage  = 0;
const uint8_t PAGES  = 4;

static bool        _wifiWasConnected = false;
static bool        _reconnecting     = false;
static uint32_t    _lastPoll         = 0;

// ── Runtime state ──────────────────────────────────────────────
enum DiagPage { DIAG_OVERVIEW, DIAG_DETAIL, DIAG_WIFI, DIAG_META };
static DiagPage    _diagPage         = DIAG_OVERVIEW;
static bool        _btnA_wasReleased = true;  // front  / left  = next page
static bool        _btnB_wasReleased = true;  // side   / right = prev page
static uint32_t    _lastDiagPollMs   = 0;
static const uint32_t DIAG_FLIP_MS  = 10000;   // auto-cycle every 10 s when connected OK

// ── Agent page sliding window ────────────────────────────────
static int _agentWin = 0;   // first visible agent row

// ── Forward declarations ──────────────────────────────────────
void runPortal();
void connectWifi();
bool pollOnce();
String errorLabel(ApiResult r);

// ═══════════════════════════════════════════════════════════════
// setup()
// ═══════════════════════════════════════════════════════════════
void setup() {
    // ── Init M5Unified once and only once — sets up display, GPIO, USB CDC ──
    auto m5cfg = M5.config();
    m5cfg.clear_display  = true;
    m5cfg.led_brightness = 0;
    M5.begin(m5cfg);

    // ── Serial — AFTER M5.begin() so USB CDC is already initialised ──
    Serial.begin(115200);

    // Boot banner (may be buffered; always println — works even if no monitor)
    Serial.println("");
    Serial.println("╔══════════════════════╗");
    Serial.println("║  HermesLens v0.2.0   ║");
    Serial.println("║  M5 StickS3 Firmware ║");
    Serial.println("╚══════════════════════╝");

    // ── Display — delegates to M5.Display (M5.begin already called above) ──
    display.init();
    display.splash("HermesLens", "Booting...");

   // ── Config ──────────────────────────────────────────────────
   // NVS (Preferences.h) — no SPIFFS needed. The default partition table
   // includes NVS at 0x9000. Config persists across reboots automatically.
   bool beginOk = configMgr.begin();
   Serial.printf("[cfg] NVS begin() -> %d (%s)\n", beginOk ? 1 : 0, beginOk ? "OK" : "FAILED");
   if (!beginOk) {
       Serial.println("[main] NVS open failed — using in-memory defaults, portal will fire");
   }
  bool haveConfig = (beginOk && configMgr.load(cfg));

    if (configMgr.needsSetup(cfg, !haveConfig)) {
        // When there's no saved config, clear the factory default backend_url
        // so the portal form starts with an empty field
        if (!haveConfig) cfg.backend_url = "";

        // Quick 2-second splash showing the AP name — user connects immediately
        for (int i = 2; i >= 0; i--) {
            Serial.printf("[main] portal in %d s  ssid='%s'  backend='%s'\n",
                          i,
                          cfg.wifi_ssid.length() > 0 ? cfg.wifi_ssid.c_str() : "(none)",
                          cfg.backend_url.length() > 0 ? cfg.backend_url.c_str() : "(none)");
            display.fillScreen(COLOR_BG);
            display.drawHeader("HermesLens Setup");
            display.drawTextCentered(52,  "Connect to WiFi:",     COLOR_WHITE);
            display.drawTextCentered(68,  "HermesLens-Setup",       COLOR_GREEN);
            display.drawTextCentered(84,  "Then open browser",    COLOR_DIM);
            display.drawTextCentered(100, "to 192.168.4.1",       COLOR_DIM);
            delay(1000);
        }
        phase = PHASE_SETUP_PORTAL;
        runPortal();
        // runPortal() restarts on success — should not get here
        display.showOffline("runPortal() returned - portal error");
        while (true) delay(1000);
    }

    // ── WiFi ─────────────────────────────────────────────────────
    phase = PHASE_WIFI;
    wifi.begin(WIFI_MODE_STA);

    phase = PHASE_HEALTH_CHECK;
    lastStatus.result = ApiResult::CONNECT_ERROR;

    phase = PHASE_DASHBOARD;
}

// ═══════════════════════════════════════════════════════════════
// loop()
// ═══════════════════════════════════════════════════════════════
void loop() {
    switch (phase) {
    case PHASE_BOOT:
        // Already handled in setup()
        phase = PHASE_DASHBOARD;
        break;

    case PHASE_DASHBOARD: {
        // ── 0. First-run render (bypasses change-detection guards) ──
        static bool _renderedFirst = false;
        if (!_renderedFirst) {
            _renderedFirst = true;
            // One-shot: force the WiFi-offline page before any guard fires.
            // We check WiFi.status() directly — wifiOK hasn't been defined yet
            // at this point in the loop.
            if (WiFi.status() != WL_CONNECTED) {
                display.fillScreen(COLOR_BG);
                display.drawHeader("HermesLens");
                display.drawTextCentered(44,  "WiFi: Offline",      COLOR_RED);
                display.drawTextCentered(60,
                    cfg.wifi_ssid.length() > 0 ? cfg.wifi_ssid.c_str() : "(none)",
                                                                  COLOR_GRAY);
                display.drawTextCentered(76,  "Connecting...",        COLOR_DIM);
                display.drawPageIndicator(_diagPage, 4);
            }
        }

        // ── 1. WiFi maintenance ─────────────────────────────────────
        bool wifiOK = wifi.maintain();
        display.setWifiStatus(wifiOK);
        if (WiFi.status() != WL_CONNECTED && !_reconnecting) {
            Serial.println("[main] WiFi down — attempting reconnect...");
            _reconnecting = true;
            wifi.begin(WIFI_MODE_STA);
            if (cfg.wifi_ssid.length() > 0) {
                wifi.connectSTA(cfg.wifi_ssid, cfg.wifi_password);
            }
        }
        if (WiFi.status() == WL_CONNECTED) _reconnecting = false;

        // ── 2. Button navigation ───────────────────────────────────
        // M5Unified: BtnA = front/logo (GPIO11), BtnB = side/right (GPIO12)
        bool prev_A = _btnA_wasReleased;
        bool prev_B = _btnB_wasReleased;
        _btnA_wasReleased = M5.BtnA.wasReleased();
        _btnB_wasReleased = M5.BtnB.wasReleased();
        if (!prev_A && _btnA_wasReleased) {  // A was just released → next page
            _diagPage = (DiagPage)((int)_diagPage + 1);
            if (_diagPage > DIAG_META) _diagPage = DIAG_OVERVIEW;
            _lastDiagPollMs = millis();   // reset auto-cycle timer
        }
        if (!prev_B && _btnB_wasReleased) {  // B was just released → prev page
            _diagPage = (DiagPage)((int)_diagPage - 1);
            if ((int)_diagPage < (int)DIAG_OVERVIEW) _diagPage = DIAG_META;
            _lastDiagPollMs = millis();
        }

        // ── 3. Poll API ────────────────────────────────────────────
        uint32_t now = millis();
        if (wifiOK && now - _lastPoll > POLL_MS) {
            _lastPoll = now;
            lastStatus = api.fetchStatus();
            Serial.printf("[main] API poll  result=%d  agents=%d\n",
                          (int)lastStatus.result,
                          (int)lastStatus.agents.agents.size());
            // auto-cycle to overview after each new poll
            _diagPage = DIAG_OVERVIEW;
            _lastDiagPollMs = now;
        }

        // ── 4. Render ──────────────────────────────────────────────
        String t1, e1, x1, x2, f1;   // tighten scope

        if (!wifiOK) {
            // Build WiFi info strings (declarations at top of block above)
            uint32_t now3 = millis();
            int rssi = WiFi.RSSI();
            String snr  = rssi > -70 ? "Good" : (rssi > -85 ? "Weak" : "Very Weak");
            String ssidStr = cfg.wifi_ssid.length() > 0 ? cfg.wifi_ssid.c_str() : "(none)";
            String ipStr   = WiFi.localIP().toString();
            String gwStr   = WiFi.gatewayIP().toString();
            // Only repaint when the page ID *or* WiFi state changes
            static bool        wasWifiOK   = false;
            static DiagPage    wasPage     = DIAG_OVERVIEW;
            static String      wasSSID     = "";
            if (wifiOK != wasWifiOK || _diagPage != wasPage || ssidStr != wasSSID) {
                wasWifiOK = wifiOK;  wasPage = _diagPage;  wasSSID = ssidStr;
                display.fillScreen(COLOR_BG);
                switch (_diagPage) {
                case DIAG_OVERVIEW:
                    display.drawHeader("HermesLens");
                    display.drawTextCentered(44,  "WiFi: Offline",           COLOR_RED);
                    display.drawTextCentered(60,  ssidStr.c_str(),          COLOR_GRAY);
                    display.drawTextCentered(76,  "Check network / 2.4 GHz", COLOR_DIM);
                    display.drawTextCentered(92,  ipStr.c_str(),            COLOR_DIM);
                    break;
                case DIAG_DETAIL:
                    display.drawHeader("WiFi Error");
                    display.drawTextCentered(44,  "Not connected",  COLOR_RED);
                    display.drawTextCentered(60,  "Connect to:",    COLOR_GRAY);
                    display.drawTextCentered(76,  ssidStr.c_str(), COLOR_WHITE);
                    display.drawTextCentered(92,  ipStr.c_str(),   COLOR_DIM);
                    break;
                case DIAG_WIFI:
                    display.drawHeader("WiFi Info");
                    display.drawTextCentered(44,  snr.c_str(),     COLOR_YELLOW);
                    display.drawTextCentered(60,  ipStr.c_str(),   COLOR_WHITE);
                    display.drawTextCentered(76,  gwStr.c_str(),   COLOR_GRAY);
                    display.drawTextCentered(92,  ssidStr.c_str(), COLOR_GRAY);
                    break;
                case DIAG_META:
                    display.drawHeader("HermesLens");
                    display.drawTextCentered(44,  "Portal ready",  COLOR_GREEN);
                    display.drawTextCentered(60,  "SSID:",         COLOR_GRAY);
                    display.drawTextCentered(76,  ssidStr.c_str(), COLOR_WHITE);
                    display.drawTextCentered(92,  ipStr.c_str(),   COLOR_DIM);
                    break;
                }
                display.drawPageIndicator(_diagPage, 4);
                display.drawTextRight(display.screenHeight() - FONT_H,
                                      _diagPage == DIAG_OVERVIEW
                                          ? "A=nxt" : "B=prv", COLOR_DIM);
            }

        } else if (lastStatus.result != ApiResult::OK) {
            // Build strings once outside switch
            String errLbl   = errorLabel(lastStatus.result);
            const char* httpStr = lastStatus.http_code > 0
                                   ? String("HTTP " + String(lastStatus.http_code)).c_str()
                                   : "";
            String errTxt   = lastStatus.error_text.length() > 0
                               ? lastStatus.error_text.c_str() : "";
            String action   = lastStatus.result == ApiResult::CONNECT_ERROR
                               ? "Check server" : "Check backend";
            // Only repaint when pageId or status changes
            static ApiResult   wasRes      = (ApiResult)99;   // impossible init
            static DiagPage    wasPageE    = DIAG_OVERVIEW;
            static String      wasErrText  = "";
            if (lastStatus.result != wasRes || _diagPage != wasPageE
                || errTxt != wasErrText)
            {
                wasRes = lastStatus.result;
                wasPageE = _diagPage;
                wasErrText = errTxt;
                switch (_diagPage) {
                case DIAG_OVERVIEW:
                    display.setDiagnosticPage(0, errLbl.c_str(),
                                              cfg.backend_url.c_str(),
                                              "A=nxt/detail  B=prv/wifi", "", "");
                    break;
                case DIAG_DETAIL:
                    display.setDiagnosticPage(1, "Detail",
                                              errLbl.c_str(),
                                              action.c_str(),
                                              errTxt.c_str(),
                                              lastStatus.gateway_state.c_str());
                    break;
                case DIAG_WIFI:
                    display.setDiagnosticPage(2, "WiFi",
                                              wifiOK ? "Connected" : "Offline",
                                              WiFi.localIP().toString().c_str(),
                                              cfg.wifi_ssid.c_str(),
                                              "IP / SSID");
                    break;
                case DIAG_META:
                    display.setDiagnosticPage(3, "Info",
                                              lastStatus.result == ApiResult::HTTP_ERROR
                                                  ? httpStr : "Conn Err",
                                              cfg.backend_url.c_str(),
                                              lastStatus.hermes_version.c_str(),
                                              "A=Overview");
                    break;
                }
                display.drawPageIndicator(_diagPage, 4);
            }
        } else {
            // ── OK path — 4-page dashboard (M8) ─────────────────────
            uint8_t pageId = (uint8_t)_diagPage;   // 0=Agents 1=Tasks 2=System 3=Usage

            // Agents page scroll: A short = next, B short = prev
            if (pageId == (uint8_t)DIAG_OVERVIEW && !prev_A && _btnA_wasReleased) {
                if (_agentWin + AGENTS_PG_VIS < (int)lastStatus.agents.agents.size())
                    _agentWin += 2;
            }
            if (pageId == (uint8_t)DIAG_OVERVIEW && !prev_B && _btnB_wasReleased) {
                _agentWin = clampWin(_agentWin - 2,
                    (int)lastStatus.agents.agents.size(), AGENTS_PG_VIS);
            }
            // Reset scroll when leaving Agents tab
            if (pageId != (uint8_t)DIAG_OVERVIEW)
                _agentWin = 0;

            switch ((int)pageId) {
            case 0:  renderAgentsPage(lastStatus.agents, pageId, true, _agentWin); break;
            case 1:  renderTasksPage  (lastStatus.tasks,  pageId, true);       break;
            case 2:  renderSystemPage (lastStatus, cfg,     pageId, true);    break;
            case 3:  renderUsagePage  (lastStatus,          pageId, true);    break;
            }
        }

        delay(100);
        break;
    }
    case PHASE_SETUP_PORTAL:
        // Handled synchronously in runPortal()
        break;
    }

    yield();
}

// ═══════════════════════════════════════════════════════════════
// runPortal()  —  setup captive portal
// ═══════════════════════════════════════════════════════════════

#include <WiFiClient.h>
#include <DNSServer.h>

static const char* AP_SSID       = "HermesLens-Setup";
static const uint16_t DNS_PORT   = 53;
static const uint16_t HTTP_PORT  = 80;

void runPortal() {
    Serial.println("[portal] Starting setup portal...");
    // ── Diagnostic overlay: show why portal fired ──────────────
    {
        String reason, missing;
        if (cfg.wifi_ssid.length() == 0  ) missing += "wifi ";
        if (cfg.backend_url.length() == 0) missing += "backend ";
        if (missing.length() > 0) {
            reason = "Setup: " + missing + "missing";
        } else {
            reason = "Setup: config incomplete";
        }
        display.showSetupDiagnostic(
            reason.c_str(),
            cfg.wifi_ssid.length() > 0 ? cfg.wifi_ssid.c_str() : "",
            cfg.backend_url.length() > 0 ? cfg.backend_url.c_str() : "",
            missing.c_str());
        delay(2500);   // give operator time to read the screen
    }
    display.showSetupMode();

    // Start AP
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(AP_SSID, "", 6, false, 4);
    if (!ok) {
        Serial.println("[portal] AP FAILED — rebooting in 5 s");
        delay(5000);
        ESP.restart();
    }
    IPAddress apIpAddr = WiFi.softAPIP();
    String apIp = apIpAddr.toString();
    Serial.printf("[portal] AP up  SSID=%s  IP=%s\n", AP_SSID, apIp.c_str());

    // DNS server — always redirects to our AP
    DNSServer dns;
    dns.start(DNS_PORT, "*", apIpAddr);

    // HTTP server (raw TCP, port 80)
    WiFiServer http(HTTP_PORT);
    http.begin();

    // Captive portal HTML (inline)
    static const char* PAGE_HTML =
        "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>HermesLens Setup</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,sans-serif}"
        "body{background:#1a1a2e;color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}"
        ".card{background:#16213e;border-radius:16px;padding:32px 28px;width:100%;max-width:420px;box-shadow:0 8px 32px rgba(0,0,0,.4)}"
        ".logo{text-align:center;margin-bottom:24px}"
        ".logo h1{font-size:24px;font-weight:700}"
        ".logo p{font-size:14px;color:#8892b0;margin-top:4px}"
        "label{display:block;font-size:13px;font-weight:600;color:#a0a8c0;margin-bottom:6px;margin-top:16px}"
        "input{width:100%;padding:14px 16px;background:#0f3460;border:2px solid transparent;border-radius:10px;color:#fff;font-size:16px;outline:none}"
        "input:focus{border-color:#00ff88}"
        "button{width:100%;padding:16px;background:#00ff88;color:#0a0a1a;border:none;border-radius:10px;font-size:17px;font-weight:700;cursor:pointer;margin-top:24px}"
        ".help{font-size:12px;color:#5a6490;margin-top:6px}"
        ".status{margin-top:16px;padding:12px;border-radius:8px;text-align:center;font-size:14px;display:none}"
        ".status.ok{background:#003820;color:#00ff88;display:block}"
        ".status.err{background:#380010;color:#ff4466;display:block}"
        "</style></head><body>"
        "<div class=\"card\"><div class=\"logo\"><h1>&#9889; HermesLens</h1>"
        "<p>M5 StickS3 Dashboard Setup</p></div>"
        "<form method=\"POST\" action=\"/save\">"
        "<label>WiFi SSID</label>"
        "<input name=\"ssid\" placeholder=\"e.g. MyHomeWiFi\" required>"
        "<div class=\"help\">2.4 GHz only</div>"
        "<label>WiFi Password</label>"
        "<input name=\"password\" placeholder=\"Enter WiFi password\" required>"
        "<label>Backend URL</label>"
        "<input name=\"backend\" placeholder=\"http://192.168.1.50:8123\" required>"
        "<div class=\"help\">Find IP: ip addr show on the server</div>"
        "<button type=\"submit\">Save & Reboot</button>"
        "</form>"
        "<div id=\"s\" class=\"status\"></div>"
        "</div>"
        "</body></html>";

    Serial.println("[portal] HTTP server ready — waiting for user...");

    bool saved = false;

    while (!saved) {
        dns.processNextRequest();

        WiFiClient client = http.available();
        if (client) {
            // ── Read request headers ─────────────────────────────────
            String req;
            {
                uint32_t deadline = millis() + 5000;  // 5 s total timeout
                while (client.connected() && millis() < deadline) {
                    while (client.available()) {
                        char c = client.read();
                        req += c;
                        if (req.indexOf( "\r\n\r\n") >= 0) {
                            deadline = 0;  // signal: headers complete
                            break;
                        }
                    }
                    if (deadline == 0) break;
                    delay(1);  // yield between TCP chunk polls
                }
            }
            yield();

            // ── Parse Content-Length (case-insensitive) ────────────
            int contentLength = 0;
            {
                String lowerReq = req;
                lowerReq.toLowerCase();
                int p = lowerReq.indexOf("content-length:");
                if (p >= 0) {
                    p += 15;
                    int q = req.indexOf('\r', p);
                    if (q < 0) q = req.length();
                    while (p < q && req[p] == ' ') p++;
                    contentLength = req.substring(p, q).toInt();
                }
            }
            if (contentLength <= 0 && req.startsWith("POST")) {
                Serial.println("[portal] WARNING: POST without content-length — skipping");
                client.println("HTTP/1.1 411 Length Required\r\nConnection: close\r\n\r\n");
                client.stop();
                continue;
            }
            if (req.startsWith("GET") && contentLength <= 0) {
                contentLength = 0;  // GET has no body — skip body parsing
            }

            // ── Read POST body ─────────────────────────────────────
            String body;
            int bodyStart = req.indexOf("\r\n\r\n");
            if (bodyStart >= 0) body = req.substring(bodyStart + 4);
            while ((int)body.length() < contentLength) {
                if (client.available()) {
                    body += (char)client.read();
                } else if (!client.connected()) {
                    Serial.printf("[portal] body aborted %d / %d bytes\n",
                                  (int)body.length(), contentLength);
                    break;
                } else {
                    delay(5);
                }
            }

            // ── Form-field parser ───────────────────────────────────
            auto bodyFieldVal = [&](const String& key) -> String {
                int keyPos = body.indexOf(key + "=");
                if (keyPos < 0) return "";
                int valStart = keyPos + key.length() + 1;
                int delim = body.indexOf('&', valStart);
                if (delim < 0) delim = body.length();
                String val = body.substring(valStart, delim);
                val.replace("+", " ");
                // URL-decode %XX
                int pct = 0;
                while (true) {
                    pct = val.indexOf('%', pct);
                    if (pct < 0 || pct + 2 >= (int)val.length()) break;
                    char hex2[3] = { val[pct+1], val[pct+2], 0 };
                    char ch = (char)strtol(hex2, nullptr, 16);
                    val = val.substring(0, pct) + ch + val.substring(pct + 3);
                    pct += 1;
                }
                return val;
            };

            // ── Router ─────────────────────────────────────────────
            if (req.startsWith("POST /save")) {
                String ssid     = bodyFieldVal("ssid");
                String password = bodyFieldVal("password");
                String backend  = bodyFieldVal("backend");
                Serial.printf("[portal] POST bytes=%d ssid=%s backend=%s\n",
                              body.length(), ssid.c_str(), backend.c_str());
                if (ssid.length() == 0 || password.length() == 0 || backend.length() == 0) {
                    String missing;
                    if (ssid.length() == 0) missing += "ssid ";
                    if (password.length() == 0) missing += "password ";
                    if (backend.length() == 0) missing += "backend ";
                    int errLen = strlen("<!doctype html><html><head><meta name=viewport content=width=device-width,initial-scale=1><title>HermesLens Setup</title><style>*{margin:0;padding:0;font:14px sans-serif}body{background:#1a1a2e;color:#fff;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px}</style></head><body><div style=background:#380010;border-radius:12px;padding:28px;display:flex;flex-direction:column;align-items:center;max-width:380px><h2 style=color:#ff4466>Issues Detected</h2><p style=color:#fff;margin:8px 0>MISSING</p><a href=/ style=color:#00ff88>Go back</a></div></body><!--missing-->");
                    char errBuf[512];
                    snprintf(errBuf, sizeof(errBuf),
                        "<!doctype html><html><head>"
                        "<meta name=viewport content=width=device-width,initial-scale=1>"
                        "<title>HermesLens Setup</title>"
                        "<style>*{margin:0;padding:0;font:14px sans-serif}"
                        "body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                        "justify-content:center;min-height:100vh;padding:20px}"
                        "</style></head><body>"
                        "<div style=background:#380010;border-radius:12px;padding:28px;"
                        "display:flex;flex-direction:column;align-items:center;max-width:380px>"
                        "<h2 style=color:#ff4466>Issues Detected</h2>"
                        "<p style=color:#fff;margin:8px 0>%s</p>"
                        "<a href=/ style=color:#00ff88>Go back</a>"
                        "</div></body></html>", missing.c_str());
                    client.printf(
                        "HTTP/1.1 400 Bad Request\r\n"
                        "Connection: close\r\n"
                        "Content-Type: text/html\r\n"
                        "Content-Length: %d\r\n\r\n",
                        (int)strlen(errBuf));
                    client.write((const uint8_t*)errBuf, strlen(errBuf));
                    client.stop();

                } else {
                    HermesConfig portalCfg;
                    portalCfg.wifi_ssid      = ssid;
                    portalCfg.wifi_password  = password;
                    portalCfg.backend_url    = backend;
                    int sv = configMgr.save(portalCfg);
                    if (sv == 0) {
                        // Verify saved by reading back
                        HermesConfig verifyCfg;
                        bool verifyOk = configMgr.load(verifyCfg);
                        Serial.printf("[portal] save-verify: load=%s  ssid='%s'  backend='%s'\n",
                                      verifyOk ? "OK" : "FAIL",
                                      verifyCfg.wifi_ssid.c_str(),
                                      verifyCfg.backend_url.c_str());
                        if (!verifyOk) {
                            Serial.println("[portal] WARNING: verify failed, flushing flash...");
                            delay(500);
                            verifyOk = configMgr.load(verifyCfg);
                            Serial.printf("[portal] save-verify retry: load=%s\n",
                                          verifyOk ? "OK" : "FAIL");
                        }
                        if (verifyOk) {
                            saved = true;
                            cfg.wifi_ssid      = ssid;
                            cfg.wifi_password  = password;
                            cfg.backend_url    = backend;
                            int saved_str_len = strlen(
                                                    "<!doctype html><html><head>"
                                                    "<meta name=viewport content=width=device-width,initial-scale=1>"
                                                    "<title>HermesLens Setup</title>"
                                                    "<style>*{margin:0;padding:0;font:14px sans-serif}"
                                                    "body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                                                    "justify-content:center;min-height:100vh;padding:20px}"
                                                    "</style></head><body>"
                                                    "<div style=background:#003820;border-radius:12px;padding:28px;"
                                                    "display:flex;flex-direction:column;align-items:center;max-width:380px>"
                                                    "<h2 style=color:#00ff88>Saved</h2>"
                                                    "<p style=color:#fff;margin:8px 0>WiFi + Backend saved &mdash; rebooting&hellip;</p>"
                                                    "</div></body></html>");
                            const char* saved_html =
                                                    "<!doctype html><html><head>"
                                                    "<meta name=viewport content=width=device-width,initial-scale=1>"
                                                    "<title>HermesLens Setup</title>"
                                                    "<style>*{margin:0;padding:0;font:14px sans-serif}"
                                                    "body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                                                    "justify-content:center;min-height:100vh;padding:20px}"
                                                    "</style></head><body>"
                                                    "<div style=background:#003820;border-radius:12px;padding:28px;"
                                                    "display:flex;flex-direction:column;align-items:center;max-width:380px>"
                                                    "<h2 style=color:#00ff88>Saved</h2>"
                                                    "<p style=color:#fff;margin:8px 0>WiFi + Backend saved &mdash; rebooting&hellip;</p>"
                                                    "</div></body></html>";
                            client.printf(
                                "HTTP/1.1 200 OK\r\n"
                                "Connection: close\r\n"
                                "Content-Type: text/html\r\n"
                                "Content-Length: %d\r\n\r\n", saved_str_len);
                            client.write((const uint8_t*)saved_html, saved_str_len);
                        } else {
                            int failLen = strlen(
                                "<!doctype html><html><head>"
                                "<meta name=viewport content=width=device-width,initial-scale=1>"
                                "<title>HermesLens Setup</title>"
                                "<style>*{margin:0;padding:0;font:14px sans-serif}"
                                "body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                                "justify-content:center;min-height:100vh;padding:20px}"
                                "</style></head><body>"
                                "<div style=background:#380010;border-radius:12px;padding:28px;"
                                "display:flex;flex-direction:column;align-items:center;max-width:380px>"
                                "<h2 style=color:#ff4466>Save Failed</h2>"
                                "<p style=color:#fff;margin:8px 0>Flash write error &mdash; press BTN-A to retry.</p>"
                                "<a href=/ style=color:#8892b0>Go back</a>"
                                "</div></body></html>");
                            const char* fail_html =
                                "<!doctype html><html><head>"
                                "<meta name=viewport content=width=device-width,initial-scale=1>"
                                "<title>HermesLens Setup</title>"
                                "<style>*{margin:0;padding:0;font:14px sans-serif}"
                                "body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                                "justify-content:center;min-height:100vh;padding:20px}"
                                "</style></head><body>"
                                "<div style=background:#380010;border-radius:12px;padding:28px;"
                                "display:flex;flex-direction:column;align-items:center;max-width:380px>"
                                "<h2 style=color:#ff4466>Save Failed</h2>"
                                "<p style=color:#fff;margin:8px 0>Flash write error &mdash; press BTN-A to retry.</p>"
                                "<a href=/ style=color:#8892b0>Go back</a>"
                                "</div></body></html>";
                            client.printf(
                                "HTTP/1.1 500 Internal Server Error\r\n"
                                "Connection: close\r\n"
                                "Content-Type: text/html\r\n"
                                "Content-Length: %d\r\n\r\n", failLen);
                            client.write((const uint8_t*)fail_html, failLen);
                        }
                    } else {
                        int failLen = strlen(
                            "<!doctype html><html><head>"
                            "<meta name=viewport content=width=device-width,initial-scale=1>"
                            "<title>HermesLens Setup</title>"
                            "<style>*{margin:0;padding:0;font:14px sans-serif}"
                            "body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                            "justify-content:center;min-height:100vh;padding:20px}"
                            "</style></head><body>"
                            "<div style=background:#380010;border-radius:12px;padding:28px;"
                            "display:flex;flex-direction:column;align-items:center;max-width:380px>"
                            "<h2 style=color:#ff4466>Save Error</h2>"
                            "<p style=color:#fff;margin:8px 0>Cannot write config &mdash; check flash.</p>"
                            "<a href=/ style=color:#8892b0>Go back</a>"
                            "</div></body></html>");
                        const char* fail_html =
                            "<!doctype html><html><head>"
                            "<meta name=viewport content=width=device-width,initial-scale=1>"
                            "<title>HermesLens Setup</title>"
                            "<style>*{margin:0;padding:0;font:14px sans-serif}"
                            "body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                            "justify-content:center;min-height:100vh;padding:20px}"
                            "</style></head><body>"
                            "<div style=background:#380010;border-radius:12px;padding:28px;"
                            "display:flex;flex-direction:column;align-items:center;max-width:380px>"
                            "<h2 style=color:#ff4466>Save Error</h2>"
                            "<p style=color:#fff;margin:8px 0>Cannot write config &mdash; check flash.</p>"
                            "<a href=/ style=color:#8892b0>Go back</a>"
                            "</div></body></html>";
                        client.printf(
                            "HTTP/1.1 500 Internal Server Error\r\n"
                            "Connection: close\r\n"
                            "Content-Type: text/html\r\n"
                            "Content-Length: %d\r\n\r\n", failLen);
                        client.write((const uint8_t*)fail_html, failLen);
                    }
                }
                client.stop();

            } else if (req.startsWith("GET /wipe")) {
                Serial.println("[portal] /wipe — clearing NVS config");
                configMgr.save(HermesConfig());  // overwrite with factory defaults
                const char* wipeHtml =
                    "<!DOCTYPE html><html><head>"
                    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                    "<title>Wipe</title>"
                    "<style>body{background:#1a1a2e;color:#fff;display:flex;align-items:center;"
                    "justify-content:center;min-height:100vh;font-family:sans-serif}"
                    ".card{background:#16213e;border-radius:16px;padding:32px;text-align:center;}"
                    "</style></head><body>"
                    "<div class=\"card\"><h1>&#10003; Config Wiped</h1>"
                    "<p>Rebooting with fresh config...</p></div></body></html>";
                int wlen = strlen(wipeHtml);
                client.printf("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                              "Connection: close\r\nContent-Length: %d\r\n\r\n", wlen);
                client.write((const uint8_t*)wipeHtml, wlen);
                client.stop();
                delay(2000);
                ESP.restart();

            } else if (req.startsWith("GET")) {
                // Serve setup page
                int len = strlen(PAGE_HTML);
                client.printf(
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "Content-Length: %d\r\n\r\n", len);
                client.write((const uint8_t*)PAGE_HTML, len);
                client.stop();
            }
        }   // if (client)
    }   // while (!saved)

    Serial.println("[portal] Config saved — rebooting into dashboard in 2 s.");
    delay(2000);
    ESP.restart();
}

// ═══════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════

String errorLabel(ApiResult r) {
    switch (r) {
    case ApiResult::OK:          return "All OK";
    case ApiResult::HTTP_ERROR:  return "HTTP Error";
    case ApiResult::PARSE_ERROR: return "Parse Error";
    case ApiResult::CONNECT_ERROR: return "Connect Error";
    default:
        Serial.printf("[main] Unknown ApiResult: %d\n", (int)r);
        return "Unknown";
    }
}

// ═══════════════════════════════════════════════════════════════
// connectWifi()  —  called once after config load
// ═══════════════════════════════════════════════════════════════
void connectWifi() {
    if (cfg.wifi_ssid.length() == 0) return;
    Serial.printf("[main] Connecting to WiFi: %s\n", cfg.wifi_ssid.c_str());
    if (wifi.connectSTA(cfg.wifi_ssid, cfg.wifi_password)) {
        Serial.printf("[main] WiFi OK  IP=%s\n", wifi.localIP().c_str());
    } else {
        Serial.println("[main] WiFi connect FAILED — will retry in main loop");
    }
}

// ═══════════════════════════════════════════════════════════════
// Button debounce helpers  (will activate in M8)
// ═══════════════════════════════════════════════════════════════
// M5StickS3: front = GPIO11  (M5 logo button)
//            side  = GPIO12  (right button)
// Both active-LOW (pulled HIGH by internal resistor).
// See M5Unified.cpp L2708 for confirmation of pin mapping.

// ═══════════════════════════════════════════════════════════════
// State mutation helpers
// ═══════════════════════════════════════════════════════════════
