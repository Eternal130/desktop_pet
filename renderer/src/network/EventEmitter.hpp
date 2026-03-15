#pragma once
#include <functional>
#include <string>
#include <nlohmann/json.hpp>
#include "network/Protocol.hpp"

namespace Network {

class EventEmitter {
public:
    using SendCallback = std::function<void(const std::string&)>;
    
    void setSendCallback(SendCallback cb);
    void emit(const std::string& action, const nlohmann::json& payload = {});
    bool isActive() const;

private:
    SendCallback _sendCallback;
};

} // namespace Network
