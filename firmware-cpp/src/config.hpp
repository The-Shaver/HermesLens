/**
 * HermesLens — Config (NVS/Preferences version)
 * =============================================
 * Persistent config using ESP32 NVS (Preferences.h).
 * No SPIFFS, no LittleFS, no custom partition table.
 * The standard partition table (built into the SDK) already includes
 * an NVS partition at 0x9000 — it's always present and never touched
 * by M5Burner or web flashers.
 *
 * Stores: WiFi SSID, WiFi password, backend URL, API timeout,
 *         refresh interval, debug flag.
 */

#ifndef HERMESLENS_CONFIG_HPP
#define HERMESLENS_CONFIG_HPP

#include <Arduino.h>
#include <Preferences.h>
#include <string>

#ifndef CONFIG_NS
#define CONFIG_NS "hermeslens"
#endif

struct HermesConfig {
    String wifi_ssid;
    String wifi_password;
    String backend_url;       // e.g. "http://127.0.0.1:8123"
    int    api_timeout;       // seconds
    int    refresh_interval;  // seconds between polls
    bool   debug_mode;

    static constexpr const char* FACTORY_BACKEND_URL = "http://13.0.0.3:8123";

    HermesConfig()
        : wifi_ssid(""),
          wifi_password(""),
          backend_url(FACTORY_BACKEND_URL),
          api_timeout(5),
          refresh_interval(10),
          debug_mode(false)
    {}
};

class ConfigManager {
public:
    ConfigManager() = default;

    /** NVS doesn't need explicit init — always succeeds. */
    bool begin() {
        return true;
    }

    /** Read config from NVS.
     *  @return true if all required keys were found and parsed.
     *  On failure 'cfg' is left as factory defaults. */
    bool load(HermesConfig& cfg) {
        Preferences prefs;
        if (!prefs.begin(CONFIG_NS, true)) {  // read-only
            Serial.println("[cfg] NVS namespace not found — using defaults");
            return false;
        }

        // Required keys: ssid, password, backend must all exist
        if (!prefs.isKey("ssid") || !prefs.isKey("password") || !prefs.isKey("backend")) {
            Serial.println("[cfg] NVS keys missing — using defaults");
            prefs.end();
            return false;
        }

        cfg.wifi_ssid        = prefs.getString("ssid", "");
        cfg.wifi_password    = prefs.getString("password", "");
        cfg.backend_url      = prefs.getString("backend", "");
        cfg.api_timeout      = prefs.getInt("api_timeout", 5);
        cfg.refresh_interval = prefs.getInt("refresh_interval", 10);
        cfg.debug_mode       = prefs.getBool("debug_mode", false);
        prefs.end();

        Serial.printf("[cfg] Loaded OK  ssid='%s'  backend='%s'\n",
                      cfg.wifi_ssid.c_str(), cfg.backend_url.c_str());
        return true;
    }

    /** Write config to NVS.
     *  NVS does not need explicit format — missing partitions are
     *  auto-initialised on first write. */
    int save(const HermesConfig& cfg) {
        Preferences prefs;
        if (!prefs.begin(CONFIG_NS, false)) {  // read-write
            Serial.println("[cfg] save() ERROR: cannot open NVS namespace");
            return -1;
        }

        prefs.putString("ssid",            cfg.wifi_ssid);
        prefs.putString("password",        cfg.wifi_password);
        prefs.putString("backend",         cfg.backend_url);
        prefs.putInt("api_timeout",        cfg.api_timeout);
        prefs.putInt("refresh_interval",   cfg.refresh_interval);
        prefs.putBool("debug_mode",        cfg.debug_mode);
        prefs.end();  // flush VFS dirty pages

        Serial.printf("[cfg] save() OK  ssid='%s'  backend='%s'\n",
                      cfg.wifi_ssid.c_str(), cfg.backend_url.c_str());
        return 0;
    }

    /** Determine whether setup portal is needed.
     *  Fires when config file is missing or required fields are empty. */
    bool needsSetup(const HermesConfig& cfg, bool load_failed) const {
        if (load_failed)                        return true;
        if (cfg.wifi_ssid.length() == 0)        return true;
        if (cfg.wifi_password.length() == 0)    return true;
        if (cfg.backend_url.length() == 0)      return true;
        return false;
    }

private:
    unsigned int save_attempts = 0;
    static constexpr unsigned int MAX_SAVE_ATTEMPTS = 5;
};

#endif  // HERMESLENS_CONFIG_HPP
