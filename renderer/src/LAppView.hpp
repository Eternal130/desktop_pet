/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include "LAppView_Common.hpp"

#ifdef USE_VULKAN
#include <vulkan/vulkan.h>
#endif

class TouchManager_Common;
class SubtitleManager;

/**
* @brief 描画クラス
*/
class LAppView : public LAppView_Common
{
public:
    /**
    * @brief コンストラクタ
    */
    LAppView();

    /**
    * @brief デストラクタ
    */
    ~LAppView();

    /**
    * @brief 初期化する。
    */
    virtual void Initialize(int width, int height) override;

    /**
    * @brief 描画する。
    */
    void Render();

    /**
    * @brief タッチされたときに呼ばれる。
    *
    * @param[in]       pointX            スクリーンX座標
    * @param[in]       pointY            スクリーンY座標
    */
    void OnTouchesBegan(float pointX, float pointY) const;

    /**
    * @brief タッチしているときにポインタが動いたら呼ばれる。
    *
    * @param[in]       pointX            スクリーンX座標
    * @param[in]       pointY            スクリーンY座標
    */
    void OnTouchesMoved(float pointX, float pointY) const;

    /**
    * @brief タッチが終了したら呼ばれる。
    *
    * @param[in]       pointX            スクリーンX座標
    * @param[in]       pointY            スクリーンY座標
    */
    void OnTouchesEnded(float pointX, float pointY) const;

    /**
    * @brief Set the subtitle manager used to draw overlay subtitles inside Render().
    */
    void SetSubtitleManager(SubtitleManager* mgr) { _subtitleManager = mgr; }

private:
#ifdef USE_VULKAN
    void BeginRendering(VkCommandBuffer cmdBuf, float r, float g, float b, float a, bool isClear);
    void EndRendering(VkCommandBuffer cmdBuf);
    void ChangeEndLayout(VkCommandBuffer cmdBuf);
#endif

    TouchManager_Common* _touchManager; ///< タッチマネージャー
    SubtitleManager* _subtitleManager = nullptr; ///< 字幕マネージャー（オプション）
};
