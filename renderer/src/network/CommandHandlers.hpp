#pragma once
#include "network/MessageHandler.hpp"

class LAppDelegate;

namespace Network {
    void RegisterCommandHandlers(MessageHandler& handler, LAppDelegate* delegate);
}
