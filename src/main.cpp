/**
 * HermesLens — Main
 * =================
 * ESP32-S3 firmware for M5 StickS3 physical dashboard.
 *
 * Boot flow:
 *   1. Serial debug at 115200
 *   2. Init LittleFS (true = format if first boot)
 *   3. Load config
 *      - If missing/incomplete → setup portal (runPortal) → reboot
 *   4. Connect WiFi STA
 *   5. Poll /api/health (smoke test)
 *   6. Main loop: maintain WiFi → poll /api/status every N sec → render page
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
#include "config.hpp"
#include "display.hpp"
#include "wifi_manager.hpp"
#include "api_client.hpp"

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

// ── Forward declarations ──────────────────────────────────────
void runPortal();
void connectWifi();
bool pollOnce();

// ═══════════════════════════════════════════════════════════════
// setup()
// ═══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n\n");
    Serial.println("╔══════════════════════╗");
    Serial.println("║  HermesLens v0.1.0   ║");
    Serial.println("║  M5 StickS3 Firmware ║");
    Serial.println("╚══════════════════════╝\n");

    // ── Init display ─────────────────────────────────────────
    display.init();
    display.splash("HermesLens", "Booting...");

    // ── Init config ──────────────────────────────────────────
    configMgr.begin();
    bool haveConfig = configMgr.load(cfg);
    if (!haveConfig || configMgr.needsSetup(cfg)) {
        phase = PHASE_SETUP_PORTAL;
        runPortal();
        // runPortal() reboots on success — should not return
        // but if it does, loop in error
        display.showOffline("");
        while (true) delay(1000);
    }

    // ── Connect WiFi ─────────────────────────────────────────
    phase = PHASE_WIFI;
    if (!wifi.begin(WIFI_MODE_STA)) {
        Serial.println("[main] WiFi STA begin failed — will retry in main loop");
    }
    // connectSTA is called in the main loop's connectWifi()

    phase = PHASE_HEALTH_CHECK;
    lastStatus.result = ApiResult::CONNECT_ERROR;  // force first fetch in main loop

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
        // 1. Maintain WiFi
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
        if (WiFi.status() == WL_CONNECTED) {
            _reconnecting = false;
        }

        // 2. Poll API on interval
        uint32_t now = millis();
        if (wifiOK && now - _lastPoll > POLL_MS) {
            _lastPoll = now;
            lastStatus = api.fetchStatus();
            Serial.printf("[main] API poll  result=%d  agents=%d\n",
                          (int)lastStatus.result,
                          (int)lastStatus.agents.agents.size());
        }

        // 3. Render
        display.fillScreen(COLOR_BG);
        if (!wifiOK) {
            display.showOffline(cfg.wifi_ssid.c_str());
        } else if (lastStatus.result != ApiResult::OK) {
            display.drawHeader("HermesLens");
            display.drawTextCentered(60, lastStatus.result == ApiResult::HTTP_ERROR
                                           ? "Backend unreachable"
                                           : "Backend error",   COLOR_RED);
            display.drawTextCentered(78,  cfg.backend_url.c_str(), COLOR_GRAY);
        } else {
            display.drawHeader("HermesLens");
            // ── Active page placeholder ───────────────────
            // TODO v1.1: agents page stub (render stubs in M8)
            display.drawTextCentered(60, "Dashboard OK", COLOR_GREEN);
            display.drawTextCentered(78, "Pages: agents/tasks/sys/usage", COLOR_GRAY);
            display.drawTextCentered(96,  String("v" + lastStatus.version).c_str(), COLOR_GRAY);
        }
        display.drawPageIndicator(currentPage, PAGES);

        // 4. TODO: button polling (M8)

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

#include <LittleFS.h>
#include <WiFiClient.h>
#include <DNSServer.h>

static const char* AP_SSID       = "HermesLens-Setup";
static const uint16_t DNS_PORT   = 53;
static const uint16_t HTTP_PORT  = 80;

void runPortal() {
    Serial.println("[portal] Starting setup portal...");
    // ── Diagnostic overlay: show why portal fired ──────────────
    // needsSetup = ssid empty OR backend empty — replicate here
    // so the display can show diagnostics before the AP goes up.
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

    // Captive portal HTML (inline to avoid needing ArduinoBearSSL / HTTP parsing libs)
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
        "<button type=\"submit\">Save &amp; Reboot</button>"
        "</form>"
        "<div id=\"s\" class=\"status\"></div>"
        "</div>"
        "</body></html>";

    Serial.println("[portal] HTTP server ready — waiting for user...");

    unsigned long deadline = millis() + 5 * 60 * 1000;  // 5-min timeout
    bool saved = false;

    while (millis() < deadline && !saved) {
        dns.processNextRequest();

        WiFiClient client = http.available();
        if (client) {
            // ── Read request headers ─────────────────────────────────
            String req;
            while (client.connected() && client.available()) {
                char c = client.read();
                req += c;
                if (req.indexOf("\r\n\r\n") > 0) break;  // end of headers
            }

            // ── Extract Content-Length ───────────────────────────────
            int contentLength = 0;
            int clHeader = req.indexOf("\r\nContent-Length:");
            if (clHeader >= 0) {
                int clEnd = req.indexOf("\r\n", clHeader + 16);
                if (clEnd < 0) clEnd = req.length();
                String clStr = req.substring(clHeader + 16, clEnd);
                clStr.trim();
                contentLength = clStr.toInt();
            }

            // ── Read POST body (may arrive in separate TCP segments) ─
            String body;
            int bodyStart = req.indexOf("\r\n\r\n");
            if (bodyStart >= 0) body = req.substring(bodyStart + 4);
            while ((int)body.length() < contentLength && client.connected()
                   && client.available()) {
                body += (char)client.read();
            }

            // Simple routing
            if (req.startsWith("POST /save")) {

                // Parse form fields (urldecode of + and %XX)
                auto fieldVal = [&](const String& key) -> String {
                    int pos = body.indexOf(key + "=");
                    if (pos < 0) return "";
                    pos += key.length() + 1;
                    int end = body.indexOf('&', pos);
                    if (end < 0) end = body.length();
                    String val = body.substring(pos, end);
                    val.replace("+", " ");
                    // minimal %XX decode
                    while (true) {
                        int pct = val.indexOf('%');
                        if (pct < 0 || pct + 2 >= val.length()) break;
                        char hex[3] = { val[pct+1], val[pct+2], 0 };
                        char ch = (char)strtol(hex, nullptr, 16);
                        val = val.substring(0, pct) + ch + val.substring(pct + 3);
                    }
                    return val;
                };

                String ssid     = fieldVal("ssid");
                String password = fieldVal("password");
                String backend  = fieldVal("backend");

                if (ssid.length() == 0 || password.length() == 0 || backend.length() == 0) {
                    String missing;
                    if (ssid.length() == 0) missing += "ssid ";
                    if (password.length() == 0) missing += "password ";
                    if (backend.length() == 0) missing += "backend ";
                    client.printf("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n{\"ok\":false,\"error\":\"missing field: %s\"}", missing.c_str());
                } else {
                    // Write config
                    StaticJsonDocument<512> doc;
                    doc["wifi_ssid"]      = ssid;
                    doc["wifi_password"]  = password;
                    doc["backend_url"]    = backend;
                    doc["api_timeout"]    = 5;
                    doc["refresh_interval"] = 10;
                    doc["debug_mode"]     = false;

                    File f = LittleFS.open(CONFIG_JSON_PATH, "w");
                    if (f) {
                        serializeJson(doc, f);
                        f.close();
                        Serial.printf("[portal] Config saved  ssid=%s  backend=%s\n",
                                      ssid.c_str(), backend.c_str());
                        client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n{\"ok\":true}");
                        saved = true;
                    } else {
                        client.println("HTTP/1.1 500\r\nConnection: close\r\n\r\n{\"ok\":false,\"error\":\"write failed\"}");
                    }
                }
                client.stop();

            } else if (req.startsWith("GET")) {
                // Serve setup page (any path)
                int len = strlen(PAGE_HTML);
                client.printf("HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html\r\n"
                              "Connection: close\r\n"
                              "Content-Length: %d\r\n\r\n", len);
                client.write((const uint8_t*)PAGE_HTML, len);
                client.stop();
            }
        }

        yield();
    }

    // Cleanup
    http.end();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    if (!saved) {
        Serial.println("[portal] Timeout — no config saved. Rebooting in 3 s.");
        delay(3000);
    } else {
        Serial.println("[portal] Config saved — rebooting into dashboard in 2 s.");
        delay(2000);
    }
    ESP.restart();  // never returns
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
