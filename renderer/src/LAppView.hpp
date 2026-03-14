/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "LAppView_Common.hpp"

class TouchManager_Common;

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

private:
    TouchManager_Common* _touchManager; ///< タッチマネージャー
};
