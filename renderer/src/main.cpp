/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include <ixwebsocket/IXNetSystem.h>
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char* argv[])
{
    UINT preConsoleOutputCP = GetConsoleOutputCP();
    SetConsoleOutputCP(65001);

    ix::initNetSystem();

    int wsPort = 9000;
    int instanceId = 0;
    std::string startupModel;
    int startupX = -1, startupY = -1;
    int startupWidth = -1, startupHeight = -1;

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            wsPort = std::atoi(argv[i + 1]);
            i++;
        }
        else if (std::strcmp(argv[i], "--instance-id") == 0 && i + 1 < argc)
        {
            instanceId = std::atoi(argv[i + 1]);
            i++;
        }
        else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc)
        {
            startupModel = argv[i + 1];
            i++;
        }
        else if (std::strcmp(argv[i], "--x") == 0 && i + 1 < argc)
        {
            startupX = std::atoi(argv[i + 1]);
            i++;
        }
        else if (std::strcmp(argv[i], "--y") == 0 && i + 1 < argc)
        {
            startupY = std::atoi(argv[i + 1]);
            i++;
        }
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            startupWidth = std::atoi(argv[i + 1]);
            i++;
        }
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            startupHeight = std::atoi(argv[i + 1]);
            i++;
        }
    }

    std::string wsUrl = "ws://localhost:" + std::to_string(wsPort)
                      + "/?instance_id=" + std::to_string(instanceId);

    LAppDelegate* delegate = LAppDelegate::GetInstance();
    delegate->SetWsUrl(wsUrl);
    if (!startupModel.empty())
        delegate->SetStartupModel(startupModel);
    if (startupX >= 0 && startupY >= 0)
        delegate->SetStartupPosition(startupX, startupY);
    if (startupWidth > 0 && startupHeight > 0)
        delegate->SetStartupSize(startupWidth, startupHeight);

    if (!delegate->Initialize())
    {
        SetConsoleOutputCP(preConsoleOutputCP);
        return 1;
    }

    delegate->Run();

    ix::uninitNetSystem();
    SetConsoleOutputCP(preConsoleOutputCP);

    return 0;
}
