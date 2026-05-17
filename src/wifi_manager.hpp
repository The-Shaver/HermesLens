/**
 * HermesLens — WiFi Manager
 * ==========================
 * Handles:
 *  - STA mode: connect to user's WiFi, reconnect on dropout
 *  - AP mode:   open "HermesLens-Setup" for captive portal (setup)
 */

#ifndef HERMESLENS_WIFI_MANAGER_HPP
#define HERMESLENS_WIFI_MANAGER_HPP

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>

class WiFiManager {
public:
    WiFiManager() = default;

    /** Start WiFi in the requested mode. Returns true when ready. */
    bool begin(wifi_mode_t mode) {
        _mode = mode;
        if (mode == WIFI_STA) {
            WiFi.mode(WIFI_STA);
            WiFi.disconnect(true);  // clear old connections, don't keep trying
            return true;
        } else {
            return startAP();
        }
    }

    /** Connect to the saved WiFi network (STA mode). Blocks up to ~10 s. */
    bool connectSTA(const String& ssid, const String& password) {
        if (ssid.length() == 0) return false;
        Serial.printf("[wifi] Connecting to SSID: %s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            delay(200);
        }
        if (WiFi.status() == WL_CONNECTED) {
            _ip = WiFi.localIP().toString();
            Serial.printf("[wifi] Connected  IP=%s\n", _ip.c_str());
            _connected = true;
            return true;
        }
        Serial.println("[wifi] Connect FAILED");
        _connected = false;
        return false;
    }

    /** Keep STA alive — reconnect if we dropped. */
    bool maintain() {
        if (_mode != WIFI_STA) return false;
        if (WiFi.status() == WL_CONNECTED) {
            if (!_connected) {
                _ip = WiFi.localIP().toString();
                Serial.printf("[wifi] Reconnected  IP=%s\n", _ip.c_str());
            }
            _connected = true;
            return true;
        }
        // Disconnected — attempt reconnect
        static uint32_t lastAttempt = 0;
        uint32_t now = millis();
        if (now - lastAttempt > 5000 && _ssid.length() > 0) {
            Serial.println("[wifi] Reconnecting...");
            WiFi.reconnect();
            lastAttempt = now;
        }
        _connected = false;
        return false;
    }

    /** AP IP (for setup portal). */
    String apIP() const { return WiFi.softAPIP().toString(); }

    bool isConnected()   const { return _connected; }
    bool apActive()      const { return _mode == WIFI_MODE_AP && _apStarted; }
    String localIP()     const { return _ip; }

private:
    bool startAP() {
        _apStarted = WiFi.softAP("HermesLens-Setup", "", 6, 0, 4);
        if (_apStarted) {
            _apIP = WiFi.softAPIP().toString();
            Serial.printf("[wifi] AP started: HermesLens-Setup  IP=%s\n", _apIP.c_str());
        } else {
            Serial.println("[wifi] AP start FAILED");
        }
        return _apStarted;
    }

    wifi_mode_t _mode      = WIFI_MODE_STA;
    bool     _connected = false;
    bool     _apStarted = false;
    String   _ssid;
    String   _ip;
    String   _apIP;
};

#endif  // HERMESLENS_WIFI_MANAGER_HPP
