/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <string>
#include <chrono>
#include <windows.h>
#include "LAppAllocator_Common.hpp"
#include "platform/WindowManager.hpp"
#include "graphics/IGraphicsBackend.hpp"

class LAppView;
class LAppTextureManager;
class AudioManager;
class SubtitleManager;

namespace Network {
    class WebSocketClient;
    class MessageHandler;
    class EventEmitter;
}

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
    void OnScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    static void GetClientSize(int& rWidth, int& rHeight);

    GLFWwindow* GetWindow() { return _windowManager->GetWindow(); }
    LAppView* GetView() { return _view; }
    void SetExecuteAbsolutePath();
    std::string GetExecuteAbsolutePath(){ return _executeAbsolutePath;}
    LAppTextureManager* GetTextureManager() { return _textureManager; }
    int GetWindowWidth() { return _windowWidth; }
    int GetWindowHeight() { return _windowHeight; }
    Network::EventEmitter* GetEventEmitter() { return _eventEmitter; }
    AudioManager* GetAudioManager() { return _audioManager; }
    SubtitleManager* GetSubtitleManager() { return _subtitleManager; }
    void SetSubtitleAdjustMode(bool enabled) { _subtitleAdjustMode = enabled; }
    IGraphicsBackend* GetGraphicsBackend() const { return _graphicsBackend; }
    void SetWsUrl(const std::string& url) { _wsUrl = url; }
    void SetStartupModel(const std::string& m) { _startupModel = m; }
    void SetStartupPosition(int x, int y) { _startupX = x; _startupY = y; _hasStartupPos = true; }
    void SetStartupSize(int w, int h) { _startupWidth = w; _startupHeight = h; _hasStartupSize = true; }
    const std::string& GetStartupModel() const { return _startupModel; }
    void ShowWindowIfHidden();
    void SetTargetFps(double fps);
    double GetTargetFps() const;
    float GetMouseX() const { return _mouseX; }
    float GetMouseY() const { return _mouseY; }

private:
    bool IsHitModel(Csm::csmFloat32 x, Csm::csmFloat32 y) const;

    LAppDelegate();
    ~LAppDelegate();

    void InitializeCubism();
    void InitializeNetwork();
    void PollNetworkMessages();

    LAppAllocator_Common _cubismAllocator;
    Csm::CubismFramework::Option _cubismOption;

    WindowManager* _windowManager;
    IGraphicsBackend* _graphicsBackend;

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

    bool _isModelDragging = false;
    float _modelDragLastX = 0.0f;
    float _modelDragLastY = 0.0f;
    bool _isClickThrough = false;

    bool _subtitleAdjustMode = false;
    float _subtitleDragLastX = 0.0f;
    float _subtitleDragLastY = 0.0f;
    float _subtitleAreaDragLastX = 0.0f;
    float _subtitleAreaDragLastY = 0.0f;

    Network::WebSocketClient* _wsClient;
    Network::MessageHandler* _messageHandler;
    Network::EventEmitter* _eventEmitter;
    bool _networkReady;

    std::string _wsUrl;
    bool _wasEverConnected;
    std::chrono::steady_clock::time_point _connectionStartTime;
    std::chrono::steady_clock::time_point _disconnectStartTime;
    bool _disconnectTimerActive;

    std::string _startupModel;
    int _startupX;
    int _startupY;
    bool _hasStartupPos;
    int _startupWidth;
    int _startupHeight;
    bool _hasStartupSize;

    AudioManager* _audioManager;
    SubtitleManager* _subtitleManager;
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

    static void OnScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        LAppDelegate::GetInstance()->OnScrollCallback(window, xoffset, yoffset);
    }
};
