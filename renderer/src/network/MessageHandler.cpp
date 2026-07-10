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
        return createResponse(msg.id, msg.action, false, 5003, "Unknown action: " + msg.action);
    }

    try {
        it->second(msg, [this](const Envelope& outbound) {
            if (_eventCallback) {
                _eventCallback(outbound);
            }
        });
        return std::nullopt;
    } catch (const std::exception& ex) {
        return createResponse(msg.id, msg.action, false, 6001, std::string("Handler exception for action ") + msg.action + ": " + ex.what());
    } catch (...) {
        return createResponse(msg.id, msg.action, false, 6001, "Handler exception for action " + msg.action);
    }
}

}
