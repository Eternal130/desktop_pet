/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include <cstdio>
#include <string>

int main(int argc, char* argv[])
{
    std::string wsUrl;
    bool showHelp = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--ws-url" && i + 1 < argc) {
            wsUrl = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            showHelp = true;
        }
    }
    
    if (showHelp) {
        printf("Usage: desktop-pet-renderer [--ws-url ws://host:port]\n");
        printf("  --ws-url  WebSocket server URL to connect to (optional)\n");
        printf("  --help    Show this help message\n");
        return 0;
    }
    
    if (!wsUrl.empty()) {
        LAppDelegate::GetInstance()->SetWebSocketUrl(wsUrl);
    }
    
    if (LAppDelegate::GetInstance()->Initialize() == GL_FALSE)
    {
        return 1;
    }

    LAppDelegate::GetInstance()->Run();

    return 0;
}

