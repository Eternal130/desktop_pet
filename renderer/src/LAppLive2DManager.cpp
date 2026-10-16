/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppLive2DManager.hpp"

#include <algorithm>
#include <string>

#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"
#include "LAppModel.hpp"
#include "network/EventEmitter.hpp"
#include "network/LoadModelRequest.hpp"

#ifdef USE_VULKAN
#include <Rendering/Vulkan/CubismDeviceInfo_Vulkan.hpp>
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
#include "graphics/VulkanBackend.hpp"
#endif

using namespace Csm;
using namespace LAppDefine;

namespace {
    LAppLive2DManager* s_instance = NULL;

    void BeganMotion(ACubismMotion* self)
    {
        LAppPal::PrintLogLn("Motion began: %x", self);
    }

    void FinishedMotion(ACubismMotion* self)
    {
        LAppPal::PrintLogLn("Motion Finished: %x", self);
    }
}

LAppLive2DManager* LAppLive2DManager::GetInstance()
{
    if (s_instance == NULL)
    {
        s_instance = new LAppLive2DManager();
    }

    return s_instance;
}

void LAppLive2DManager::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

LAppLive2DManager::LAppLive2DManager()
    : _viewMatrix(NULL)
{
    _viewMatrix = new CubismMatrix44();

    const std::string& model = LAppDelegate::GetInstance()->GetStartupModel();
    if (!model.empty() && Network::isAbsoluteModel3JsonPath(model))
    {
        // v1.13 plugin-scoped models: --model may carry an absolute
        // .model3.json path (the controller resolves scope before spawning).
        // Name mode stays byte-identical — names can never be absolute.
        LoadModelFromPath(model);
    }
    else
    {
        ChangeScene(model.empty() ? "Hiyori" : model.c_str());
    }
}

LAppLive2DManager::~LAppLive2DManager()
{
#ifdef USE_VULKAN
    // Cubism SDK 5-r.5: per-device CubismDeviceInfo_Vulkan replaces the offscreen singleton
    Csm::Rendering::CubismDeviceInfo_Vulkan::ReleaseAllDeviceInfo();
#endif
    ReleaseAllModel();
    delete _viewMatrix;
}

void LAppLive2DManager::LoadModel(const csmChar* modelName)
{
    csmString modelPath(LAppDelegate::GetInstance()->GetExecuteAbsolutePath().c_str());
    modelPath += ResourcesPath;
    modelPath += modelName;
    modelPath.Append(1, '/');

    csmString modelJsonName(modelName);
    modelJsonName += ".model3.json";

#ifdef USE_VULKAN
    {
        auto* vkBackend = static_cast<VulkanBackend*>(
            LAppDelegate::GetInstance()->GetGraphicsBackend());
        vkDeviceWaitIdle(vkBackend->GetDevice());
    }
#endif

    ReleaseAllModel();

    _models.PushBack(new LAppModel());
#ifdef USE_VULKAN
    {
        auto* vkBackend = static_cast<VulkanBackend*>(
            LAppDelegate::GetInstance()->GetGraphicsBackend());
        _models[0]->LoadAssets(
            vkBackend->GetDevice(),
            vkBackend->GetImageFormat(),
            modelPath.GetRawString(),
            modelJsonName.GetRawString());
    }
#else
    _models[0]->LoadAssets(modelPath.GetRawString(), modelJsonName.GetRawString());
#endif

    _hitAreaNames = _models[0]->GetHitAreaNames();
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("[APP]auto-populated %d hit areas from model", static_cast<int>(_hitAreaNames.size()));
    }
}

bool LAppLive2DManager::LoadModelFromPath(const std::string& absJsonPath)
{
    if (absJsonPath.empty() || !LAppPal::FileExists(absJsonPath))
    {
        LAppPal::PrintLogLn("[APP]LoadModelFromPath: model json not found: %s", absJsonPath.c_str());
        return false;
    }

    // Split into dir + fileName the same way LoadModel feeds LoadAssets
    // (dir keeps its trailing separator; fileName is the .model3.json name).
    const size_t pos = absJsonPath.find_last_of("/\\");
    const std::string dir = (pos == std::string::npos)
        ? std::string()
        : absJsonPath.substr(0, pos + 1);
    const std::string fileName = (pos == std::string::npos)
        ? absJsonPath
        : absJsonPath.substr(pos + 1);

    csmString modelPath(dir.c_str());
    csmString modelJsonName(fileName.c_str());

#ifdef USE_VULKAN
    {
        auto* vkBackend = static_cast<VulkanBackend*>(
            LAppDelegate::GetInstance()->GetGraphicsBackend());
        vkDeviceWaitIdle(vkBackend->GetDevice());
    }
#endif

    ReleaseAllModel();

    _models.PushBack(new LAppModel());
#ifdef USE_VULKAN
    {
        auto* vkBackend = static_cast<VulkanBackend*>(
            LAppDelegate::GetInstance()->GetGraphicsBackend());
        _models[0]->LoadAssets(
            vkBackend->GetDevice(),
            vkBackend->GetImageFormat(),
            modelPath.GetRawString(),
            modelJsonName.GetRawString());
    }
#else
    _models[0]->LoadAssets(modelPath.GetRawString(), modelJsonName.GetRawString());
#endif

    _hitAreaNames = _models[0]->GetHitAreaNames();
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("[APP]auto-populated %d hit areas from model", static_cast<int>(_hitAreaNames.size()));
    }
    return true;
}

void LAppLive2DManager::ReleaseAllModel()
{
    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        delete _models[i];
    }

    _models.Clear();
}

LAppModel* LAppLive2DManager::GetModel(csmUint32 no) const
{
    if (no < _models.GetSize())
    {
        return _models[no];
    }

    return NULL;
}

void LAppLive2DManager::SetRenderTargetSize(csmUint32 width, csmUint32 height)
{
    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        LAppModel* model = GetModel(i);

        model->SetRenderTargetSize(width, height);
    }
}

void LAppLive2DManager::OnDrag(csmFloat32 x, csmFloat32 y) const
{
    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        LAppModel* model = GetModel(i);

        model->SetDragging(x, y);
    }
}

void LAppLive2DManager::OnTap(csmFloat32 x, csmFloat32 y)
{
    if (DebugLogEnable)
    {
        LAppPal::PrintLogLn("[APP]tap point: {x:%.2f y:%.2f}", x, y);
    }

    for (csmUint32 i = 0; i < _models.GetSize(); i++)
    {
        auto* delegate = LAppDelegate::GetInstance();
        auto* emitter = delegate->GetEventEmitter();
        bool networkActive = emitter && emitter->isActive();

        for (const auto& areaName : _hitAreaNames)
        {
            if (_models[i]->HitTest(areaName.c_str(), x, y))
            {
                if (DebugLogEnable)
                {
                    LAppPal::PrintLogLn("[APP]hit area: [%s]", areaName.c_str());
                }
                if (networkActive)
                {
                    std::string areaId(areaName);
                    std::transform(areaId.begin(), areaId.end(), areaId.begin(), ::tolower);
                    emitter->emit("hit", {{"area_id", areaId}, {"x", delegate->GetMouseX()}, {"y", delegate->GetMouseY()}, {"button", 0}});
                }
                else
                {
                    _models[i]->SetRandomExpression();
                }
                break;
            }
        }
    }
}

void LAppLive2DManager::SetHitAreaNames(const std::vector<std::string>& names)
{
    _hitAreaNames = names;
}

const std::vector<std::string>& LAppLive2DManager::GetHitAreaNames() const
{
    return _hitAreaNames;
}

void LAppLive2DManager::OnUpdate() const
{
#ifdef USE_VULKAN
    {
        auto* vkBackend = static_cast<VulkanBackend*>(
            LAppDelegate::GetInstance()->GetGraphicsBackend());
        Csm::Rendering::CubismDeviceInfo_Vulkan::GetDeviceInfo(vkBackend->GetDevice())
            ->GetOffscreenManager()->BeginFrameProcess();
        Live2D::Cubism::Framework::Rendering::CubismRenderer_Vulkan::SetRenderTarget(
            vkBackend->GetSwapchainImage(),
            vkBackend->GetSwapchainImageView(),
            vkBackend->GetSwapchainImageFormat(),
            vkBackend->GetSwapchainExtent());
    }
#endif

    int width = LAppDelegate::GetInstance()->GetWindowWidth();
    int height = LAppDelegate::GetInstance()->GetWindowHeight();

    csmUint32 modelCount = _models.GetSize();
    for (csmUint32 i = 0; i < modelCount; ++i)
    {
        CubismMatrix44 projection;
        LAppModel* model = GetModel(i);

        if (model->GetModel() == NULL)
        {
            LAppPal::PrintLogLn("Failed to model->GetModel().");
            continue;
        }

        if (model->GetModel()->GetCanvasWidth() > 1.0f && width < height)
        {
            model->GetModelMatrix()->SetWidth(2.0f);
            projection.Scale(1.0f, static_cast<float>(width) / static_cast<float>(height));
        }
        else
        {
            projection.Scale(static_cast<float>(height) / static_cast<float>(width), 1.0f);
        }

        if (_viewMatrix != NULL)
        {
            projection.MultiplyByMatrix(_viewMatrix);
        }

        model->Update();
        model->Draw(projection);
    }

#ifdef USE_VULKAN
    {
        auto* vkBackend = static_cast<VulkanBackend*>(
            LAppDelegate::GetInstance()->GetGraphicsBackend());
        auto* offscreenManager = Csm::Rendering::CubismDeviceInfo_Vulkan::GetDeviceInfo(
            vkBackend->GetDevice())->GetOffscreenManager();
        offscreenManager->EndFrameProcess();
        offscreenManager->ReleaseStaleRenderTextures();
    }
#endif
}

void LAppLive2DManager::ChangeScene(const csmChar* modelName)
{
    LoadModel(modelName);
}

csmUint32 LAppLive2DManager::GetModelNum() const
{
    return _models.GetSize();
}

void LAppLive2DManager::SetViewMatrix(CubismMatrix44* m)
{
    for (int i = 0; i < 16; i++) {
        _viewMatrix->GetArray()[i] = m->GetArray()[i];
    }
}
