#include "network/EventEmitter.hpp"

namespace Network {

void EventEmitter::setSendCallback(SendCallback cb) {
    _sendCallback = std::move(cb);
}

void EventEmitter::emit(const std::string& action, const nlohmann::json& payload) {
    if (!_sendCallback) return;
    auto event = createEvent(action, payload);
    _sendCallback(serialize(event));
}

bool EventEmitter::isActive() const {
    return static_cast<bool>(_sendCallback);
}

}
