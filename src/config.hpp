/**
 * HermesLens — Config
 * ===================
 * Persistent config over LittleFS.
 * Stores: WiFi SSID, WiFi password, backend URL, API timeout,
 *         refresh interval, page order, debug flag.
 * Falls back to factory defaults if no config file exists.
 */

#ifndef HERMESLENS_CONFIG_HPP
#define HERMESLENS_CONFIG_HPP

#include <Arduino.h>
#include <LittleFS.h>
#include <string>
#include <vector>
#include <ArduinoJson.h>

#ifndef CONFIG_JSON_PATH
#define CONFIG_JSON_PATH "/config.json"
#endif

struct HermesConfig {
    String wifi_ssid;
    String wifi_password;
    String backend_url;       // e.g. "http://192.168.1.50:8123"
    int    api_timeout;       // seconds
    int    refresh_interval;  // seconds between polls
    bool   debug_mode;

    // defaults
    HermesConfig()
        : wifi_ssid(""),
          wifi_password(""),
          backend_url("http://192.168.1.100:8123"),
          api_timeout(5),
          refresh_interval(10),
          debug_mode(false)
    {}
};

class ConfigManager {
public:
    ConfigManager() = default;

    bool begin() {
        return LittleFS.begin(true);  // true = format if mount fails
    }

    bool load(HermesConfig& cfg) {
        File f = LittleFS.open(CONFIG_JSON_PATH, "r");
        if (!f) {
            Serial.println("[cfg] No config.json — using defaults");
            return false;
        }
        String json = f.readString();
        f.close();

        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, json) != DeserializationError::Ok) {
            Serial.println("[cfg] config.json corrupt — using defaults");
            return false;
        }

        cfg.wifi_ssid        = doc["wifi_ssid"]        | cfg.wifi_ssid;
        cfg.wifi_password    = doc["wifi_password"]    | cfg.wifi_password;
        cfg.backend_url      = doc["backend_url"]      | cfg.backend_url;
        cfg.api_timeout      = doc["api_timeout"]      | cfg.api_timeout;
        cfg.refresh_interval = doc["refresh_interval"] | cfg.refresh_interval;
        cfg.debug_mode       = doc["debug_mode"]       | cfg.debug_mode;

        Serial.printf("[cfg] Loaded: ssid=%s  backend=%s\n",
                      cfg.wifi_ssid.c_str(), cfg.backend_url.c_str());
        return true;
    }

    bool save(const HermesConfig& cfg) {
        StaticJsonDocument<512> doc;
        doc["wifi_ssid"]        = cfg.wifi_ssid;
        doc["wifi_password"]    = cfg.wifi_password;
        doc["backend_url"]      = cfg.backend_url;
        doc["api_timeout"]      = cfg.api_timeout;
        doc["refresh_interval"] = cfg.refresh_interval;
        doc["debug_mode"]       = cfg.debug_mode;

        File f = LittleFS.open(CONFIG_JSON_PATH, "w");
        if (!f) {
            Serial.println("[cfg] FAILED to open config.json for writing");
            return false;
        }
        if (serializeJson(doc, f) == 0) {
            Serial.println("[cfg] FAILED to write config.json");
            f.close();
            return false;
        }
        f.close();
        Serial.println("[cfg] Config saved");
        return true;
    }

    /** Returns true if WiFi + backend are configured (first-time setup check). */
    bool needsSetup(const HermesConfig& cfg) const {
        return cfg.wifi_ssid.length() == 0 || cfg.backend_url.length() == 0;
    }

    /** Lists files on LittleFS (debug / status). */
    void listFS() const {
        Serial.println("[cfg] LittleFS contents:");
        File root = LittleFS.open("/");
        File f = root.openNextFile();
        while (f) {
            Serial.printf("  %-24s  %d bytes\n", f.name(), f.size());
            f = root.openNextFile();
        }
    }
};

#endif  // HERMESLENS_CONFIG_HPP
