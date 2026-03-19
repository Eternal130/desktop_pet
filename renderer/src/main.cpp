/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char* argv[])
{
    UINT preConsoleOutputCP = GetConsoleOutputCP();
    SetConsoleOutputCP(65001);

    int wsPort = 9000;
    int instanceId = 0;

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
    }

    std::string wsUrl = "ws://localhost:" + std::to_string(wsPort)
                      + "/?instance_id=" + std::to_string(instanceId);
    LAppDelegate::GetInstance()->SetWsUrl(wsUrl);

    if (LAppDelegate::GetInstance()->Initialize() == GL_FALSE)
    {
        SetConsoleOutputCP(preConsoleOutputCP);
        return 1;
    }

    LAppDelegate::GetInstance()->Run();

    SetConsoleOutputCP(preConsoleOutputCP);

    return 0;
}
