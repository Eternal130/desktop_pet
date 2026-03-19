#include "WebSocketClient.hpp"
#include "LAppPal.hpp"
#include <GLFW/glfw3.h>

namespace Network {

WebSocketClient::WebSocketClient() = default;

WebSocketClient::~WebSocketClient() {
    _ws.stop();
}

bool WebSocketClient::connect(const std::string& url) {
    _ws.setUrl(url);

    _ws.setOnMessageCallback([this, url](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (_messageQueue.size() >= kMaxQueueSize) {
                _messageQueue.pop();
                LAppPal::PrintLogLn("[WebSocketClient] Message queue overflow, discarding oldest message");
            }
            _messageQueue.push(msg->str);
            glfwPostEmptyEvent();
        } else if (msg->type == ix::WebSocketMessageType::Open) {
            LAppPal::PrintLogLn("[WebSocketClient] Connected to: %s", url.c_str());
        } else if (msg->type == ix::WebSocketMessageType::Close) {
            LAppPal::PrintLogLn("[WebSocketClient] Disconnected");
        } else if (msg->type == ix::WebSocketMessageType::Error) {
            LAppPal::PrintLogLn("[WebSocketClient] Error: %s", msg->errorInfo.reason.c_str());
        }
    });

    _ws.start();
    return true;
}

void WebSocketClient::disconnect() {
    _ws.stop();
}

void WebSocketClient::send(const std::string& message) {
    _ws.sendText(message);
}

bool WebSocketClient::isConnected() const {
    return _ws.getReadyState() == ix::ReadyState::Open;
}

std::vector<std::string> WebSocketClient::drainMessages() {
    std::vector<std::string> messages;
    std::lock_guard<std::mutex> lock(_queueMutex);
    while (!_messageQueue.empty()) {
        messages.push_back(std::move(_messageQueue.front()));
        _messageQueue.pop();
    }
    return messages;
}

} // namespace Network
