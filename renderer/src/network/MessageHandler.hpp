#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "network/Protocol.hpp"

namespace Network {

using CommandHandler = std::function<void(const Envelope& cmd, std::function<void(const Envelope&)> sendResponse)>;
using EventCallback = std::function<void(const Envelope& event)>;

class MessageHandler {
public:
    void registerCommand(const std::string& action, CommandHandler handler);
    void setEventCallback(EventCallback callback);
    std::optional<Envelope> dispatch(const Envelope& msg);

private:
    std::unordered_map<std::string, CommandHandler> _handlers;
    EventCallback _eventCallback;
};

}
