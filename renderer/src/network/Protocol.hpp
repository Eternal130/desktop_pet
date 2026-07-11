#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace Network {

struct Envelope {
    std::string type;
    std::string action;
    std::string id;
    nlohmann::json payload;
    int64_t timestamp;
    bool success = true;
    int error_code = 0;
    std::string error_message;
};

// Protocol action/event/error constants (must mirror controller network/Protocol.java)
inline constexpr const char* ACTION_GET_STATS = "get_stats";
inline constexpr const char* EVENT_STATS_STATE = "stats_state";
inline constexpr int ERROR_STATS_COLLECTION_FAILED = 9001;

// Subtitle actions
inline constexpr const char* ACTION_SHOW_SUBTITLE = "show_subtitle";
inline constexpr const char* ACTION_HIDE_SUBTITLE = "hide_subtitle";
inline constexpr const char* ACTION_SET_SUBTITLE_STYLE = "set_subtitle_style";
inline constexpr const char* ACTION_SET_SUBTITLE_ADJUST_MODE = "set_subtitle_adjust_mode";
inline constexpr const char* ACTION_SET_SUBTITLE_LAYOUT = "set_subtitle_layout";

// Subtitle error codes (10000-10099)
inline constexpr int ERROR_SUBTITLE_TEXT_REQUIRED = 10001;
inline constexpr int ERROR_SUBTITLE_NOT_INITIALIZED = 10002;

std::string serialize(const Envelope& env);
std::optional<Envelope> deserialize(const std::string& json_str);
Envelope createCommand(const std::string& action, const nlohmann::json& payload = {});
Envelope createEvent(const std::string& action, const nlohmann::json& payload = {});
Envelope createResponse(const std::string& original_id, const std::string& action, bool success, int error_code = 0, const std::string& error_message = "");
std::string generateId();

} 
