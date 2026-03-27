/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

// GLEW must be included before GLFW (which pulls in gl.h)
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "graphics/OpenGLBackend.hpp"
#include "LAppDelegate.hpp"
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

    if (!_windowManager->Initialize(RenderTargetWidth, RenderTargetHeight,
                                       _hasStartupPos, _startupX, _startupY,
                                       _hasStartupSize, _startupWidth, _startupHeight))
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("Can't initialize window.");
        }
        return false;
    }

    if (!_graphicsBackend->InitializeGraphics(_windowManager->GetWindow()))
    {
        if (DebugLogEnable)
        {
            LAppPal::PrintLogLn("Can't initialize graphics.");
        }
        return false;
    }

    GLFWwindow* window = _windowManager->GetWindow();
    glfwSetMouseButtonCallback(window, EventHandler::OnMouseCallBack);
    glfwSetCursorPosCallback(window, EventHandler::OnMouseCallBack);
    glfwSetScrollCallback(window, EventHandler::OnScrollCallback);

    _windowManager->GetWindowSize(_windowWidth, _windowHeight);
    _graphicsBackend->BeginFrame(_windowWidth, _windowHeight);

    // Cubismの初期化
    InitializeCubism();

    SetExecuteAbsolutePath();

    //load model
    LAppLive2DManager::GetInstance();

    //AppViewの初期化
    _view->Initialize(_windowWidth, _windowHeight);

    _audioManager = new AudioManager();
    if (!_audioManager->Init()) {
        LAppPal::PrintLogLn("[LAppDelegate] Audio engine init failed, continuing without audio");
    }

    InitializeNetwork();

    return true;
}

void LAppDelegate::Release()
{
    if (_audioManager) { _audioManager->Uninit(); }
    delete _audioManager; _audioManager = nullptr;

    if (_wsClient) { _wsClient->disconnect(); }
    delete _eventEmitter; _eventEmitter = nullptr;
    delete _messageHandler; _messageHandler = nullptr;
    delete _wsClient; _wsClient = nullptr;

    _graphicsBackend->ReleaseGraphics();
    _windowManager->Release();

    delete _graphicsBackend; _graphicsBackend = nullptr;
    delete _windowManager; _windowManager = nullptr;

    delete _textureManager;
    delete _view;

    LAppLive2DManager::ReleaseInstance();

    CubismFramework::Dispose();
}

void LAppDelegate::Run()
{
    //メインループ
    while (!_windowManager->ShouldClose())
    {
        int width, height;
        _windowManager->GetWindowSize(width, height);
        if((_windowWidth!=width || _windowHeight!=height) && width>0 && height>0)
        {
            _view->Initialize(width, height);
            // モデルのレンダーターゲットのサイズ変更
            LAppLive2DManager::GetInstance()->SetRenderTargetSize(width, height);
            _windowWidth = width;
            _windowHeight = height;
        }

        // 時間更新
        LAppPal::UpdateTime();

        // Global cursor eye tracking — use Win32 GetCursorPos to track beyond GLFW window bounds
        if (!_isDragging)
        {
            POINT cursorPos;
            if (GetCursorPos(&cursorPos))
            {
                int windowX, windowY;
                _windowManager->GetWindowPosition(windowX, windowY);

                float localX = static_cast<float>(cursorPos.x - windowX);
                float localY = static_cast<float>(cursorPos.y - windowY);

                float viewX = _view->TransformViewX(localX);
                float viewY = _view->TransformViewY(localY);

                LAppLive2DManager::GetInstance()->OnDrag(viewX, viewY);
            }
        }

        // 画面の初期化 (alpha=0 for transparency)
        _graphicsBackend->BeginFrame(_windowWidth, _windowHeight);

        //描画更新
        _view->Render();

        if (!_isDragging && !_captured)
        {
            POINT cursorPos;
            if (GetCursorPos(&cursorPos))
            {
                int windowX, windowY;
                _windowManager->GetWindowPosition(windowX, windowY);
                int localX = cursorPos.x - windowX;
                int localY = cursorPos.y - windowY;

                if (localX >= 0 && localX < _windowWidth && localY >= 0 && localY < _windowHeight)
                {
                    bool isTransparent = _graphicsBackend->IsPixelTransparent(localX, localY, _windowHeight);
                    if (isTransparent != _isClickThrough)
                    {
                        _windowManager->SetMousePassthrough(isTransparent);
                        _isClickThrough = isTransparent;
                    }
                }
            }
        }
        else
        {
            if (_isClickThrough)
            {
                _windowManager->SetMousePassthrough(false);
                _isClickThrough = false;
            }
        }

        // バッファの入れ替え
        _graphicsBackend->EndFrame(_windowManager->GetWindow());

        PollNetworkMessages();

        if (!_windowManager->IsWindowShown())
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - _connectionStartTime).count();
            if (elapsed > 5)
            {
                _windowManager->ShowWindow();
            }
        }

        double fps = _windowManager->GetTargetFps();
        if (fps > 0.0)
        {
            _windowManager->WaitEvents(1.0 / fps);
        }
        else
        {
            _windowManager->WaitEvents(1.0 / 60.0);
        }
    }

    Release();

    LAppDelegate::ReleaseInstance();
}

LAppDelegate::LAppDelegate():
    _cubismOption(),
    _windowManager(new WindowManager()),
    _graphicsBackend(new OpenGLBackend()),
    _captured(false),
    _mouseX(0.0f),
    _mouseY(0.0f),
    _windowWidth(0),
    _windowHeight(0),
    _isDragging(false),
    _dragStartX(0.0),
    _dragStartY(0.0),
    _isClickThrough(false),
    _wsClient(nullptr),
    _messageHandler(nullptr),
    _eventEmitter(nullptr),
    _networkReady(false),
    _wsUrl("ws://localhost:9000"),
    _wasEverConnected(false),
    _connectionStartTime(std::chrono::steady_clock::now()),
    _startupX(0),
    _startupY(0),
    _hasStartupPos(false),
    _startupWidth(0),
    _startupHeight(0),
    _hasStartupSize(false),
    _audioManager(nullptr)
{
    _executeAbsolutePath = "";
    _view = new LAppView();
    _textureManager = new LAppTextureManager();
    _textureManager->SetGraphicsBackend(_graphicsBackend);
}

LAppDelegate::~LAppDelegate()
{

}

void LAppDelegate::SetTargetFps(double fps)
{
    _windowManager->SetTargetFps(fps);
    if (fps <= 0.0)
    {
        LAppPal::PrintLogLn("[LAppDelegate] FPS mode: adaptive (VSync)");
    }
    else
    {
        LAppPal::PrintLogLn("[LAppDelegate] FPS mode: fixed %.0f", fps);
    }
}

double LAppDelegate::GetTargetFps() const
{
    return _windowManager->GetTargetFps();
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
                    _windowManager->GetWindowPosition(wx, wy);
                    _eventEmitter->emit("drag_end", {{"x", static_cast<float>(_mouseX)}, {"y", static_cast<float>(_mouseY)}, {"window_x", wx}, {"window_y", wy}});
                }
            }
        }
        return;
    }

    if (GLFW_MOUSE_BUTTON_LEFT == button &&
        (_windowManager->IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
         _windowManager->IsKeyPressed(GLFW_KEY_RIGHT_SHIFT)))
    {
        if (GLFW_PRESS == action)
        {
            _isModelDragging = true;
            _modelDragLastX = _mouseX;
            _modelDragLastY = _mouseY;
        }
        else if (GLFW_RELEASE == action && _isModelDragging)
        {
            _isModelDragging = false;

            LAppModel* model = LAppLive2DManager::GetInstance()->GetModel(0);
            if (model && _eventEmitter && _eventEmitter->isActive())
            {
                _eventEmitter->emit("layout_changed", {
                    {"offset_x", model->GetUserOffsetX()},
                    {"offset_y", model->GetUserOffsetY()},
                    {"scale", model->GetUserScale()}
                });
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
        _windowManager->SetWindowPosition(pt.x - static_cast<int>(_dragStartX),
                                        pt.y - static_cast<int>(_dragStartY));
        return;
    }

    if (_isModelDragging)
    {
        float dx = _mouseX - _modelDragLastX;
        float dy = _mouseY - _modelDragLastY;

        float ndcDx =  dx * 2.0f / static_cast<float>(_windowHeight);
        float ndcDy = -dy * 2.0f / static_cast<float>(_windowHeight);

        LAppModel* model = LAppLive2DManager::GetInstance()->GetModel(0);
        if (model)
        {
            model->AdjustUserOffset(ndcDx, ndcDy);
        }

        _modelDragLastX = _mouseX;
        _modelDragLastY = _mouseY;
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
    bool shiftPressed = (_windowManager->IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
                         _windowManager->IsKeyPressed(GLFW_KEY_RIGHT_SHIFT));
    bool ctrlPressed = (_windowManager->IsKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                         _windowManager->IsKeyPressed(GLFW_KEY_RIGHT_CONTROL));

    if (shiftPressed)
    {
        float factor = 1.0f + static_cast<float>(yoffset) * 0.1f;
        LAppModel* model = LAppLive2DManager::GetInstance()->GetModel(0);
        if (model)
        {
            model->AdjustUserScale(factor);

            if (_eventEmitter && _eventEmitter->isActive())
            {
                _eventEmitter->emit("layout_changed", {
                    {"offset_x", model->GetUserOffsetX()},
                    {"offset_y", model->GetUserOffsetY()},
                    {"scale", model->GetUserScale()}
                });
            }
        }
        return;
    }

    if (!ctrlPressed)
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
    _windowManager->GetWindowPosition(posX, posY);

    // Center-pivot: keep window center stable across resize
    int centerX = posX + currentWidth / 2;
    int centerY = posY + currentHeight / 2;
    int newPosX = centerX - newWidth / 2;
    int newPosY = centerY - newHeight / 2;

    _windowManager->SetWindowSize(newWidth, newHeight);
    _windowManager->SetWindowPosition(newPosX, newPosY);

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
    LAppDelegate::GetInstance()->_windowManager->GetWindowSize(rWidth, rHeight);
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
                _windowManager->SetShouldClose();
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
    _windowManager->ShowWindow();
}
