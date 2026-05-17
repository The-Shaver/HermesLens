/**
 * HermesLens — API Client
 * ========================
 * Polls the HermesLens backend:
 *   GET /api/health  → liveness check
 *   GET /api/status  → full dashboard JSON
 *
 * Uses Arduino's built-in HTTPClient (part of esp32-arduino framework,
 * no extra library needed) + ArduinoJson 6.x for deserialization.
 */

#ifndef HERMESLENS_API_CLIENT_HPP
#define HERMESLENS_API_CLIENT_HPP

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string>

struct AgentsData {
    struct Agent {
        String name;
        String role;
        String status;
        int    task_progress;
        String current_task;
        String model;
    };
    std::vector<Agent> agents;
};

struct TasksData {
    int ready;
    int in_progress;
    int blocked;
    int done;
    int archived;
};

struct SessionsData {
    int   total;
    int   active;
    int   today;
    int   tokens_input;
    int   tokens_output;
    int   tool_calls_total;
    float estimated_cost_usd;
};

struct HeaderBlock {
    String name;
    String value;
};

enum class ApiResult {
    OK,           // 200, good JSON
    HTTP_ERROR,   // non-200 HTTP response
    PARSE_ERROR,  // 200 but bad / unparseable JSON
    CONNECT_ERROR // WiFi down, DNS fail, timeout
};

struct ApiStatus {
    ApiResult         result = ApiResult::CONNECT_ERROR;
    String            version;
    String            hermes_version;
    String            gateway_state;
    SessionsData      sessions;
    TasksData         tasks;
    AgentsData        agents;
};

class ApiClient {
public:
    ApiClient(const String& baseUrl = "", int timeoutMs = 5000)
        : _baseUrl(baseUrl), _timeoutMs(timeoutMs) {}

    void setUrl(const String& url) { _baseUrl = url; }
    void setTimeoutMs(int ms)       { _timeoutMs = ms; }

    /** GET /api/health — 1 kB payload, no parsing needed. */
    ApiResult fetchHealth() {
        if (_baseUrl.length() == 0 || !WiFi.isConnected()) return ApiResult::CONNECT_ERROR;
        HTTPClient http;
        String url = _baseUrl + "/api/health";
        http.begin(url);
        http.setTimeout(_timeoutMs);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int code = http.GET();
        http.end();
        return (code == 200) ? ApiResult::OK : ApiResult::HTTP_ERROR;
    }

    /** GET /api/status — full dashboard JSON. */
    ApiStatus fetchStatus() {
        ApiStatus out;
        if (_baseUrl.length() == 0 || !WiFi.isConnected()) return out;

        HTTPClient http;
        String url = _baseUrl + "/api/status";
        http.begin(url);
        http.setTimeout(_timeoutMs);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            out.result = ApiResult::HTTP_ERROR;
            http.end();
            return out;
        }

        int bodyLen = http.getSize();
        if (bodyLen <= 0) {
            out.result = ApiResult::PARSE_ERROR;
            http.end();
            return out;
        }

        // Stream into a DynamicJsonDocument (ESP32 has enough PSRAM for ~16 KB)
        DynamicJsonDocument doc(8192);
        WiFiClient* stream = http.getStreamPtr();
        DeserializationError err = deserializeJson(doc, *stream);
        http.end();

        if (err) {
            Serial.printf("[api] JSON parse error: %s\n", err.f_str());
            out.result = ApiResult::PARSE_ERROR;
            return out;
        }

        out.result      = ApiResult::OK;
        out.version     = doc["version"]          | "";
        out.hermes_version = doc["hermes_version"] | "";

        JsonObject gw = doc["gateway"];
        out.gateway_state = gw["state"] | "unknown";

        // Agents
        JsonArray ja = doc["agents"]["agents"];
        for (JsonObject a : ja) {
            AgentsData::Agent agent;
            agent.name            = a["name"]            | "";
            agent.role            = a["role"]            | "";
            agent.status          = a["status"]           | "idle";
            agent.task_progress   = a["task_progress"]    | 0;
            agent.current_task    = a["current_task"]     | "";
            agent.model           = a["model"]            | "";
            out.agents.agents.push_back(agent);
        }

        // Tasks
        JsonObject jt = doc["tasks"];
        out.tasks.ready        = jt["ready"]        | 0;
        out.tasks.in_progress  = jt["in_progress"]  | 0;
        out.tasks.blocked      = jt["blocked"]      | 0;
        out.tasks.done         = jt["done"]         | 0;
        out.tasks.archived     = jt["archived"]     | 0;

        // Sessions / usage
        JsonObject js = doc["sessions"];
        out.sessions.total                = js["total"]                 | 0;
        out.sessions.active               = js["active"]                | 0;
        out.sessions.today                = js["today"]                 | 0;
        out.sessions.tokens_input         = js["tokens_input"]          | 0;
        out.sessions.tokens_output        = js["tokens_output"]         | 0;
        out.sessions.tool_calls_total     = js["tool_calls_total"]      | 0;
        out.sessions.estimated_cost_usd   = js["estimated_cost_usd"]    | 0.0f;

        return out;
    }

private:
    String _baseUrl;
    int    _timeoutMs;
};

#endif  // HERMESLENS_API_CLIENT_HPP
