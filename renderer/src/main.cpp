/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include <windows.h>

int main()
{
    UINT preConsoleOutputCP = GetConsoleOutputCP();
    SetConsoleOutputCP(65001);

    if (LAppDelegate::GetInstance()->Initialize() == GL_FALSE)
    {
        SetConsoleOutputCP(preConsoleOutputCP);
        return 1;
    }

    LAppDelegate::GetInstance()->Run();

    SetConsoleOutputCP(preConsoleOutputCP);

    return 0;
}
