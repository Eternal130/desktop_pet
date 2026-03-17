/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <string>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "LAppAllocator_Common.hpp"

class LAppView;
class LAppTextureManager;

/**
* @brief   アプリケーションクラス。
*   Cubismの管理を行う。
*/
class LAppDelegate
{
public:
    static LAppDelegate* GetInstance();
    static void ReleaseInstance();

    bool Initialize();
    void Release();
    void Run();

    void OnMouseCallBack(GLFWwindow* window, int button, int action, int modify);
    void OnMouseCallBack(GLFWwindow* window, double x, double y);

    static void GetClientSize(int& rWidth, int& rHeight);

    GLFWwindow* GetWindow() { return _window; }
    LAppView* GetView() { return _view; }
    void SetExecuteAbsolutePath();
    std::string GetExecuteAbsolutePath(){ return _executeAbsolutePath;}
    LAppTextureManager* GetTextureManager() { return _textureManager; }
    int GetWindowWidth() { return _windowWidth; }
    int GetWindowHeight() { return _windowHeight; }

private:
    bool IsHitModel(Csm::csmFloat32 x, Csm::csmFloat32 y) const;

    LAppDelegate();
    ~LAppDelegate();

    void InitializeCubism();

    LAppAllocator_Common _cubismAllocator;
    Csm::CubismFramework::Option _cubismOption;
    GLFWwindow* _window;
    LAppView* _view;
    bool _captured;
    float _mouseX;
    float _mouseY;
    LAppTextureManager* _textureManager;
    std::string _executeAbsolutePath;

    int _windowWidth;
    int _windowHeight;

    bool _isDragging;
    double _dragStartX;
    double _dragStartY;
    int _windowStartX;
    int _windowStartY;
};

class EventHandler
{
public:
    static void OnMouseCallBack(GLFWwindow* window, int button, int action, int modify)
    {
        LAppDelegate::GetInstance()->OnMouseCallBack(window, button, action, modify);
    }

    static void OnMouseCallBack(GLFWwindow* window, double x, double y)
    {
         LAppDelegate::GetInstance()->OnMouseCallBack(window, x, y);
    }
};
