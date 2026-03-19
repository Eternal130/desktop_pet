#include "network/MessageHandler.hpp"

#include <exception>

namespace Network {

void MessageHandler::registerCommand(const std::string& action, CommandHandler handler) {
    _handlers[action] = std::move(handler);
}

void MessageHandler::setEventCallback(EventCallback callback) {
    _eventCallback = std::move(callback);
}

std::optional<Envelope> MessageHandler::dispatch(const Envelope& msg) {
    if (msg.type == "response") {
        return std::nullopt;
    }

    const auto it = _handlers.find(msg.action);
    if (it == _handlers.end()) {
        return createEvent("error", {{"error_code", 5003}, {"error_message", "Unknown action: " + msg.action}});
    }

    try {
        it->second(msg, [this](const Envelope& outbound) {
            if (_eventCallback) {
                _eventCallback(outbound);
            }
        });
        return std::nullopt;
    } catch (const std::exception& ex) {
        return createEvent("error", {{"error_code", 5003}, {"error_message", std::string("Handler exception for action ") + msg.action + ": " + ex.what()}});
    } catch (...) {
        return createEvent("error", {{"error_code", 5003}, {"error_message", "Handler exception for action " + msg.action}});
    }
}

}
