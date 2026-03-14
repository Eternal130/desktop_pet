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

void LAppView::Render()
{
    LAppLive2DManager* live2DManager = LAppLive2DManager::GetInstance();
    live2DManager->SetViewMatrix(_viewMatrix);
    live2DManager->OnUpdate();
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
