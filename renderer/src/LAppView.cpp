/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppView.hpp"

#include "LAppPal.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppDefine.hpp"
#include "TouchManager_Common.hpp"

#ifdef USE_VULKAN
#include "graphics/VulkanBackend.hpp"
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
#include "LAppDelegate.hpp"
#endif

using namespace LAppDefine;

LAppView::LAppView()
    : LAppView_Common()
{
    _touchManager = new TouchManager_Common();
}

LAppView::~LAppView()
{
    if (_touchManager)
    {
        delete _touchManager;
    }
}

void LAppView::Initialize(int width, int height)
{
    LAppView_Common::Initialize(width, height);
}

#ifdef USE_VULKAN
void LAppView::BeginRendering(VkCommandBuffer cmdBuf, float r, float g, float b, float a, bool isClear)
{
    auto* vkBackend = static_cast<VulkanBackend*>(
        LAppDelegate::GetInstance()->GetGraphicsBackend());

    VkRenderingAttachmentInfoKHR colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    colorAttachment.imageView = vkBackend->GetSwapchainImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = isClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{r, g, b, a}};

    VkRenderingInfoKHR renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderingInfo.renderArea = {{0, 0}, vkBackend->GetSwapchainExtent()};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmdBuf, &renderingInfo);
}

void LAppView::EndRendering(VkCommandBuffer cmdBuf)
{
    vkCmdEndRendering(cmdBuf);
}
#endif

void LAppView::Render()
{
#ifdef USE_VULKAN
    auto* vkBackend = static_cast<VulkanBackend*>(
        LAppDelegate::GetInstance()->GetGraphicsBackend());

    // Step 1: Clear swapchain with transparent background
    VkCommandBuffer cmdBuf = vkBackend->BeginSingleTimeCommands();
    BeginRendering(cmdBuf, 0, 0, 0, 0, true);
    EndRendering(cmdBuf);
    vkBackend->SubmitCommand(cmdBuf, true);

    // Step 2: Live2D model rendering
    // (The layout transition to PRESENT_SRC_KHR moved into
    // VulkanBackend::EndFrame's per-frame tail command buffer.)
    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->SetViewMatrix(_viewMatrix);
    live2DManager->OnUpdate();
#else
    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->SetViewMatrix(_viewMatrix);
    live2DManager->OnUpdate();
#endif
}

void LAppView::OnTouchesBegan(float px, float py) const
{
    _touchManager->TouchesBegan(px, py);
}

void LAppView::OnTouchesMoved(float px, float py) const
{
    _touchManager->TouchesMoved(px, py);

    const float viewX = TransformViewX(_touchManager->GetX());
    const float viewY = TransformViewY(_touchManager->GetY());

    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->OnDrag(viewX, viewY);
}

void LAppView::OnTouchesEnded(float px, float py) const
{
    _touchManager->TouchesMoved(px, py);

    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->OnDrag(0.0f, 0.0f);

    const float x = TransformScreenX(_touchManager->GetX());
    const float y = TransformScreenY(_touchManager->GetY());
    if (DebugTouchLogEnable)
    {
        LAppPal::PrintLogLn("[APP]touchesEnded x:%.2f y:%.2f", x, y);
    }
    live2DManager->OnTap(x, y);
}
