/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppDelegate.hpp"
#include <windows.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "LAppView.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppTextureManager.hpp"
#include "network/WebSocketClient.hpp"
#include "network/MessageHandler.hpp"
#include "network/EventEmitter.hpp"
#include "network/CommandHandlers.hpp"
#include "network/Protocol.hpp"
#include "AudioManager.hpp"

using namespace Csm;
using namespace std;
using namespace LAppDefine;

namespace {
    LAppDelegate* s_instance = NULL;
}

LAppDelegate* LAppDelegate::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppDelegate();
    }

    return s_instance;
}

void LAppDelegate::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

bool LAppDelegate::Initialize()
{
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("START");
    }

    // GLFWの初期化
    if (glfwInit() == GL_FALSE)
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("Can't initilize GLFW");
        }
        return GL_FALSE;
    }

    // Desktop pet: transparent, borderless, always-on-top, initially hidden
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    // Windowの生成_
    int initWidth = _hasStartupSize ? _startupWidth : RenderTargetWidth;
    int initHeight = _hasStartupSize ? _startupHeight : RenderTargetHeight;
    _window = glfwCreateWindow(initWidth, initHeight, "desktop-pet-renderer", NULL, NULL);
    if (_window == NULL)
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("Can't create GLFW window.");
        }
        glfwTerminate();
        return GL_FALSE;
    }

    if (_hasStartupPos)
    {
        glfwSetWindowPos(_window, _startupX, _startupY);
    }

    // Windowのコンテキストをカレントに設定
    glfwMakeContextCurrent(_window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("Can't initilize glew.");
        }
        glfwTerminate();
        return GL_FALSE;
    }

    //テクスチャサンプリング設定
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    //透過設定
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenBuffers(1, &_pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);
    GLubyte zeros[4] = {0, 0, 0, 0};
    glBufferData(GL_PIXEL_PACK_BUFFER, 4, zeros, GL_STREAM_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    //コールバック関数の登録
    glfwSetMouseButtonCallback(_window, EventHandler::OnMouseCallBack);
    glfwSetCursorPosCallback(_window, EventHandler::OnMouseCallBack);
    glfwSetScrollCallback(_window, EventHandler::OnScrollCallback);

    // ウィンドウサイズ記憶
    int width, height;
    glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width, &height);
    _windowWidth = width;
    _windowHeight = height;
    glViewport(0, 0, _windowWidth, _windowHeight);

    // Cubismの初期化
    InitializeCubism();

    SetExecuteAbsolutePath();

    //load model
    LAppLive2DManager::GetInstance();

    //AppViewの初期化
    _view->Initialize(width, height);

    _audioManager = new AudioManager();
    if (!_audioManager->Init()) {
        LAppPal::PrintLogLn("[LAppDelegate] Audio engine init failed, continuing without audio");
    }

    InitializeNetwork();

    return GL_TRUE;
}

void LAppDelegate::Release()
{
    if (_audioManager) { _audioManager->Uninit(); }
    delete _audioManager; _audioManager = nullptr;

    if (_wsClient) { _wsClient->disconnect(); }
    delete _eventEmitter; _eventEmitter = nullptr;
    delete _messageHandler; _messageHandler = nullptr;
    delete _wsClient; _wsClient = nullptr;

    if (_pbo)
    {
        glDeleteBuffers(1, &_pbo);
        _pbo = 0;
    }

    glfwDestroyWindow(_window);

    glfwTerminate();

    delete _textureManager;
    delete _view;

    LAppLive2DManager::ReleaseInstance();

    CubismFramework::Dispose();
}

void LAppDelegate::Run()
{
    //メインループ
    while (glfwWindowShouldClose(_window) == GL_FALSE)
    {
        int width, height;
        glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &width, &height);
        if((_windowWidth!=width || _windowHeight!=height) && width>0 && height>0)
        {
            _view->Initialize(width, height);
            // モデルのレンダーターゲットのサイズ変更
            LAppLive2DManager::GetInstance()->SetRenderTargetSize(width, height);
            _windowWidth = width;
            _windowHeight = height;
        }
        glViewport(0, 0, _windowWidth, _windowHeight);

        // 時間更新
        LAppPal::UpdateTime();

        // Global cursor eye tracking — use Win32 GetCursorPos to track beyond GLFW window bounds
        if (!_isDragging)
        {
            POINT cursorPos;
            if (GetCursorPos(&cursorPos))
            {
                int windowX, windowY;
                glfwGetWindowPos(_window, &windowX, &windowY);

                float localX = static_cast<float>(cursorPos.x - windowX);
                float localY = static_cast<float>(cursorPos.y - windowY);

                float viewX = _view->TransformViewX(localX);
                float viewY = _view->TransformViewY(localY);

                LAppLive2DManager::GetInstance()->OnDrag(viewX, viewY);
            }
        }

        // 画面の初期化 (alpha=0 for transparency)
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glClearDepth(1.0);

        //描画更新
        _view->Render();

        if (!_isDragging && !_captured)
        {
            POINT cursorPos;
            if (GetCursorPos(&cursorPos))
            {
                int windowX, windowY;
                glfwGetWindowPos(_window, &windowX, &windowY);
                int localX = cursorPos.x - windowX;
                int localY = cursorPos.y - windowY;

                if (localX >= 0 && localX < _windowWidth && localY >= 0 && localY < _windowHeight)
                {
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);
                    GLubyte* ptr = static_cast<GLubyte*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
                    GLubyte alpha = 0;
                    if (ptr)
                    {
                        alpha = ptr[3];
                        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                    }

                    int fbY = _windowHeight - 1 - localY;
                    glReadPixels(localX, fbY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

                    bool shouldBeClickThrough = (alpha == 0);
                    if (shouldBeClickThrough != _isClickThrough)
                    {
                        glfwSetWindowAttrib(_window, GLFW_MOUSE_PASSTHROUGH, shouldBeClickThrough ? GLFW_TRUE : GLFW_FALSE);
                        _isClickThrough = shouldBeClickThrough;
                    }
                }
            }
        }
        else
        {
            if (_isClickThrough)
            {
                glfwSetWindowAttrib(_window, GLFW_MOUSE_PASSTHROUGH, GLFW_FALSE);
                _isClickThrough = false;
            }
        }

        // バッファの入れ替え
        glfwSwapBuffers(_window);

        PollNetworkMessages();

        if (!_windowShown)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - _connectionStartTime).count();
            if (elapsed > 5)
            {
                ShowWindowIfHidden();
            }
        }

        if (_targetFps > 0.0)
        {
            glfwWaitEventsTimeout(1.0 / _targetFps);
        }
        else
        {
            glfwWaitEventsTimeout(1.0 / 60.0);
        }
    }

    Release();

    LAppDelegate::ReleaseInstance();
}

LAppDelegate::LAppDelegate():
    _cubismOption(),
    _window(NULL),
    _captured(false),
    _mouseX(0.0f),
    _mouseY(0.0f),
    _windowWidth(0),
    _windowHeight(0),
    _isDragging(false),
    _dragStartX(0.0),
    _dragStartY(0.0),
    _wsClient(nullptr),
    _messageHandler(nullptr),
    _eventEmitter(nullptr),
    _networkReady(false),
    _targetFps(0.0),
    _wsUrl("ws://localhost:9000"),
    _wasEverConnected(false),
    _connectionStartTime(std::chrono::steady_clock::now()),
    _startupX(0),
    _startupY(0),
    _hasStartupPos(false),
    _startupWidth(0),
    _startupHeight(0),
    _hasStartupSize(false),
    _windowShown(false),
    _pbo(0),
    _isClickThrough(false),
    _audioManager(nullptr)
{
    _executeAbsolutePath = "";
    _view = new LAppView();
    _textureManager = new LAppTextureManager();
}

LAppDelegate::~LAppDelegate()
{

}

void LAppDelegate::SetTargetFps(double fps)
{
    _targetFps = fps;
    if (fps <= 0.0)
    {
        glfwSwapInterval(1);
        LAppPal::PrintLogLn("[LAppDelegate] FPS mode: adaptive (VSync)");
    }
    else
    {
        glfwSwapInterval(0);
        LAppPal::PrintLogLn("[LAppDelegate] FPS mode: fixed %.0f", fps);
    }
}

void LAppDelegate::InitializeCubism()
{
    //setup cubism
    _cubismOption.LogFunction = LAppPal::PrintMessage;
    _cubismOption.LoggingLevel = LAppDefine::CubismLoggingLevel;
    _cubismOption.LoadFileFunction = LAppPal::LoadFileAsBytes;
    _cubismOption.ReleaseBytesFunction = LAppPal::ReleaseBytes;
    Csm::CubismFramework::StartUp(&_cubismAllocator, &_cubismOption);

    //Initialize cubism
    CubismFramework::Initialize();

    //default proj
    CubismMatrix44 projection;

    LAppPal::UpdateTime();
}

void LAppDelegate::OnMouseCallBack(GLFWwindow* window, int button, int action, int modify)
{
    if (_view == NULL)
    {
        return;
    }

    if (GLFW_MOUSE_BUTTON_MIDDLE == button)
    {
        if (GLFW_PRESS == action)
        {
            float x = _view->TransformScreenX(_mouseX);
            float y = _view->TransformScreenY(_mouseY);

            if (IsHitModel(x, y))
            {
                _isDragging = true;
                _dragStartX = _mouseX;
                _dragStartY = _mouseY;

                if (_eventEmitter && _eventEmitter->isActive())
                {
                    _eventEmitter->emit("drag_start", {{"x", static_cast<float>(_mouseX)}, {"y", static_cast<float>(_mouseY)}});
                }
            }
        }
        else if (GLFW_RELEASE == action)
        {
            if (_isDragging)
            {
                _isDragging = false;

                if (_eventEmitter && _eventEmitter->isActive())
                {
                    int wx, wy;
                    glfwGetWindowPos(_window, &wx, &wy);
                    _eventEmitter->emit("drag_end", {{"x", static_cast<float>(_mouseX)}, {"y", static_cast<float>(_mouseY)}, {"window_x", wx}, {"window_y", wy}});
                }
            }
        }
        return;
    }

    if (GLFW_MOUSE_BUTTON_LEFT != button)
    {
        return;
    }

    if (GLFW_PRESS == action)
    {
        _captured = true;
        _view->OnTouchesBegan(_mouseX, _mouseY);
    }
    else if (GLFW_RELEASE == action)
    {
        if (_captured)
        {
            _captured = false;
            _view->OnTouchesEnded(_mouseX, _mouseY);
        }
    }
}

void LAppDelegate::OnMouseCallBack(GLFWwindow* window, double x, double y)
{
    _mouseX = static_cast<float>(x);
    _mouseY = static_cast<float>(y);

    if (_isDragging)
    {
        POINT pt;
        GetCursorPos(&pt);
        glfwSetWindowPos(_window, pt.x - static_cast<int>(_dragStartX),
                                  pt.y - static_cast<int>(_dragStartY));
        return;
    }

    if (_view)
    {
        _view->OnTouchesMoved(_mouseX, _mouseY);
    }

    if (!_captured)
    {
        return;
    }
    if (_view == NULL)
    {
        return;
    }
}

void LAppDelegate::OnScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    // Ctrl + scroll = window resize
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) != GLFW_PRESS &&
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) != GLFW_PRESS)
    {
        return;
    }

    int currentWidth = _windowWidth;
    int currentHeight = _windowHeight;
    if (currentWidth <= 0 || currentHeight <= 0)
    {
        return;
    }

    // 5% per scroll tick
    float scaleFactor = 1.0f + static_cast<float>(yoffset) * 0.05f;

    int newWidth = static_cast<int>(currentWidth * scaleFactor);
    int newHeight = static_cast<int>(currentHeight * scaleFactor);

    const int minDim = 100;
    const int maxDim = 2000;
    newWidth = max(minDim, min(newWidth, maxDim));
    newHeight = max(minDim, min(newHeight, maxDim));

    if (newWidth == currentWidth && newHeight == currentHeight)
    {
        return;
    }

    int posX, posY;
    glfwGetWindowPos(window, &posX, &posY);

    // Center-pivot: keep window center stable across resize
    int centerX = posX + currentWidth / 2;
    int centerY = posY + currentHeight / 2;
    int newPosX = centerX - newWidth / 2;
    int newPosY = centerY - newHeight / 2;

    glfwSetWindowSize(window, newWidth, newHeight);
    glfwSetWindowPos(window, newPosX, newPosY);

    LAppPal::PrintLogLn("[LAppDelegate] Window resized: %dx%d -> %dx%d, pos: (%d,%d)",
        currentWidth, currentHeight, newWidth, newHeight, newPosX, newPosY);

    if (_eventEmitter && _eventEmitter->isActive())
    {
        _eventEmitter->emit("window_resized", {
            {"window_width", newWidth},
            {"window_height", newHeight},
            {"window_x", newPosX},
            {"window_y", newPosY}
        });
    }
}

void LAppDelegate::GetClientSize(int& rWidth, int& rHeight)
{
    glfwGetWindowSize(LAppDelegate::GetInstance()->GetWindow(), &rWidth, &rHeight);
}

void LAppDelegate::SetExecuteAbsolutePath()
{
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);

    if (len > 0 && len < MAX_PATH)
    {
        // Find the last backslash or forward slash
        char* lastSep = nullptr;
        for (DWORD i = 0; i < len; ++i)
        {
            if (path[i] == '\\' || path[i] == '/')
            {
                lastSep = &path[i];
            }
        }
        if (lastSep)
        {
            *(lastSep + 1) = '\0';
        }
    }

    this->_executeAbsolutePath = path;
}

bool LAppDelegate::IsHitModel(Csm::csmFloat32 x, Csm::csmFloat32 y) const
{
    LAppLive2DManager* manager = LAppLive2DManager::GetInstance();
    const auto& hitAreaNames = manager->GetHitAreaNames();
    const Csm::csmUint32 modelCount = manager->GetModelNum();
    for (Csm::csmUint32 i = 0; i < modelCount; ++i)
    {
        LAppModel* model = manager->GetModel(i);
        if (!model)
        {
            continue;
        }

        for (const auto& areaName : hitAreaNames)
        {
            if (model->HitTest(areaName.c_str(), x, y))
            {
                return true;
            }
        }
    }

    return false;
}

void LAppDelegate::InitializeNetwork()
{
    _wsClient = new Network::WebSocketClient();
    _messageHandler = new Network::MessageHandler();
    _eventEmitter = new Network::EventEmitter();

    _eventEmitter->setSendCallback([this](const std::string& msg) {
        _wsClient->send(msg);
    });

    _messageHandler->setEventCallback([this](const Network::Envelope& env) {
        _wsClient->send(Network::serialize(env));
    });

    Network::RegisterCommandHandlers(*_messageHandler, this);

    _connectionStartTime = std::chrono::steady_clock::now();
    _wsClient->connect(_wsUrl);
    LAppPal::PrintLogLn("[Network] Connecting to controller at %s", _wsUrl.c_str());
}

void LAppDelegate::PollNetworkMessages()
{
    if (!_wsClient) return;

    bool connected = _wsClient->isConnected();

    if (connected)
    {
        _wasEverConnected = true;

        if (!_networkReady)
        {
            _networkReady = true;
            if (_eventEmitter)
            {
                _eventEmitter->emit("ready", {{"version", "1.0.0"}, {"capabilities", nlohmann::json::array({"live2d"})}});
            }
            LAppPal::PrintLogLn("[Network] Sent ready event to controller");
        }
    }
    else
    {
        if (_networkReady)
        {
            _networkReady = false;
            LAppPal::PrintLogLn("[Network] Connection lost, waiting for reconnection...");
        }

        if (!_wasEverConnected)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - _connectionStartTime).count();
            if (elapsed > 10)
            {
                LAppPal::PrintLogLn("[Network] Connection timeout (%llds), exiting...", elapsed);
                glfwSetWindowShouldClose(_window, GLFW_TRUE);
            }
        }
        return;
    }

    auto messages = _wsClient->drainMessages(50);
    for (const auto& msg : messages)
    {
        auto env = Network::deserialize(msg);
        if (env)
        {
            auto response = _messageHandler->dispatch(*env);
            if (response)
            {
                _wsClient->send(Network::serialize(*response));
            }
        }
    }
}

void LAppDelegate::ShowWindowIfHidden()
{
    if (_windowShown) return;
    _windowShown = true;
    glfwShowWindow(_window);
}
