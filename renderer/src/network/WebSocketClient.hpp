#pragma once
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <ixwebsocket/IXWebSocket.h>

namespace Network {

class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();

    bool connect(const std::string& url);
    void disconnect();
    void send(const std::string& message);
    bool isConnected() const;
    std::vector<std::string> drainMessages(size_t maxCount = kMaxQueueSize);

private:
    ix::WebSocket _ws;
    mutable std::mutex _queueMutex;
    std::queue<std::string> _messageQueue;
    static constexpr size_t kMaxQueueSize = 1000;
};

} // namespace Network
