#include "network/Protocol.hpp"

#include <chrono>
#include <random>

namespace {

int64_t currentTimestampMs() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

}

namespace Network {

std::string serialize(const Envelope& env) {
    nlohmann::json j;
    j["type"] = env.type;
    j["action"] = env.action;
    j["id"] = env.id;
    j["payload"] = env.payload;
    j["timestamp"] = env.timestamp;

    if (env.type == "response") {
        j["success"] = env.success;
        j["error_code"] = env.error_code;
        j["error_message"] = env.error_message;
    }

    return j.dump();
}

std::optional<Envelope> deserialize(const std::string& json_str) {
    if (json_str.empty()) {
        return std::nullopt;
    }

    try {
        const auto j = nlohmann::json::parse(json_str);

        if (!j.contains("type") || !j.contains("action") || !j.contains("id") || !j.contains("payload") || !j.contains("timestamp")) {
            return std::nullopt;
        }

        Envelope env;
        env.type = j.at("type").get<std::string>();
        env.action = j.at("action").get<std::string>();
        env.id = j.at("id").get<std::string>();
        env.payload = j.at("payload");
        env.timestamp = j.at("timestamp").get<int64_t>();

        if (env.type == "response") {
            env.success = j.value("success", true);
            env.error_code = j.value("error_code", 0);
            env.error_message = j.value("error_message", std::string());
        }

        return env;
    } catch (...) {
        return std::nullopt;
    }
}

std::string generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::uniform_int_distribution<uint64_t> dist;
    const auto value = dist(rng);
    return std::to_string(value);
}

Envelope createCommand(const std::string& action, const nlohmann::json& payload) {
    Envelope env;
    env.type = "command";
    env.action = action;
    env.id = generateId();
    env.payload = payload;
    env.timestamp = currentTimestampMs();
    return env;
}

Envelope createEvent(const std::string& action, const nlohmann::json& payload) {
    Envelope env;
    env.type = "event";
    env.action = action;
    env.id = generateId();
    env.payload = payload;
    env.timestamp = currentTimestampMs();
    return env;
}

Envelope createResponse(const std::string& original_id, const std::string& action, bool success, int error_code, const std::string& error_message) {
    Envelope env;
    env.type = "response";
    env.action = action;
    env.id = original_id;
    env.payload = nlohmann::json::object();
    env.timestamp = currentTimestampMs();
    env.success = success;
    env.error_code = error_code;
    env.error_message = error_message;
    return env;
}

}
