/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <CubismFramework.hpp>
#include <ICubismModelSetting.hpp>
#include <Type/csmRectF.hpp>
#include "LAppWavFileHandler_Common.hpp"
#include "LAppModel_Common.hpp"

#include <string>
#include <vector>

#ifdef USE_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace Network { class EventEmitter; }

/**
 * @brief ユーザーが実際に使用するモデルの実装クラス<br>
 *         モデル生成、機能コンポーネント生成、更新処理とレンダリングの呼び出しを行う。
 *
 */
class LAppModel : public LAppModel_Common
{
public:
    /**
     * @brief コンストラクタ
     */
    LAppModel();

    /**
     * @brief デストラクタ
     *
     */
    virtual ~LAppModel();

    /**
     * @brief model3.jsonが置かれたディレクトリとファイルパスからモデルを生成する
     *
     */
    void LoadAssets(const Csm::csmChar* dir, const  Csm::csmChar* fileName);

    /**
     * @brief レンダラを再構築する
     *
     */
    void ReloadRenderer();

#ifdef USE_VULKAN
    /**
     * @brief Vulkan overload: Load model assets with Vulkan device info
     */
    void LoadAssets(VkDevice device, VkFormat imageFormat, const Csm::csmChar* dir, const Csm::csmChar* fileName);

    /**
     * @brief Vulkan overload: Rebuild renderer with Vulkan device info
     */
    void ReloadRenderer(VkDevice device, VkFormat surfaceFormat);
#endif

    /**
     * @brief   モデルの更新処理。モデルのパラメータから描画状態を決定する。
     *
     */
    void Update();

    /**
     * @brief   モデルを描画する処理。モデルを描画する空間のView-Projection行列を渡す。
     *
     * @param[in]  matrix  View-Projection行列
     */
    void Draw(Csm::CubismMatrix44& matrix);

    /**
     * @brief   引数で指定したモーションの再生を開始する。
     *
     * @param[in]   group                       モーショングループ名
     * @param[in]   no                          グループ内の番号
     * @param[in]   priority                    優先度
     * @param[in]   onFinishedMotionHandler     モーション再生終了時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @param[in]   onBeganMotionHandler        モーション再生開始時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @return                                  開始したモーションの識別番号を返す。個別のモーションが終了したか否かを判定するIsFinished()の引数で使用する。開始できない時は「-1」
     */
    Csm::CubismMotionQueueEntryHandle StartMotion(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority, Csm::ACubismMotion::FinishedMotionCallback onFinishedMotionHandler = NULL, Csm::ACubismMotion::BeganMotionCallback onBeganMotionHandler = NULL);

    Csm::CubismMotionQueueEntryHandle StartMotionWithCustomData(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority, Csm::ACubismMotion::FinishedMotionCallback onFinishedMotionHandler, void* customData);

    bool StartMotionFromFile(const std::string& filePath, int priority,
                                     float fadeIn, float fadeOut, Network::EventEmitter* emitter);

    /**
     * @brief   再生中モーションの進捗スナップショット（motion_progress WS イベント用）
     *
     *          直近にプロトコル経由（play_motion / play_motion_ext）で開始した
     *          モーションが「再生中」であるか、その識別子と進捗を返す。
     *          進捗は CubismMotionQueueEntry 自身のタイムラインから計算する：
     *          GetStateTime()（最終 UpdateMotion 時刻）と GetStartTime()/GetEndTime()
     *          （AdjustEndTime が start + Duration を設定。ループモーションは -1）
     *          は同一の motion manager クロック域にあり、枠外の時計を要しない。
     *
     * @return  スナップショット。active=false は「追跡中の再生なし」＝
     *          呼び出し側は何も送信しない（アイドル時ゼロ発火）。
     *          hasProgress=false は duration 不明（ループ／開始直前）で
     *          progress フィールドを省略すべきことを意味する（偽装値禁止）。
     */
    struct MotionProgressSnapshot {
        bool active = false;          ///< tracked motion is playing right now
        bool isPathVariant = false;   ///< play_motion_ext identity (motion_path) vs play_motion (group/index)
        std::string group;            ///< group variant: motion group ("" for path variant)
        int index = -1;               ///< group variant: group index (-1 for path variant)
        std::string path;             ///< path variant: motion file path ("" for group variant)
        bool hasProgress = false;     ///< false → duration unknown: omit progress
        float progress = 0.0f;        ///< [0,1], valid only when hasProgress
    };
    MotionProgressSnapshot GetMotionProgress();


    /**
     * @brief   ランダムに選ばれたモーションの再生を開始する。
     *
     * @param[in]   group                       モーショングループ名
     * @param[in]   priority                    優先度
     * @param[in]   onFinishedMotionHandler     モーション再生終了時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @param[in]   onBeganMotionHandler        モーション再生開始時に呼び出されるコールバック関数。NULLの場合、呼び出されない。
     * @return                                  開始したモーションの識別番号を返す。個別のモーションが終了したか否かを判定するIsFinished()の引数で使用する。開始できない時は「-1」
     */
    Csm::CubismMotionQueueEntryHandle StartRandomMotion(const Csm::csmChar* group, Csm::csmInt32 priority, Csm::ACubismMotion::FinishedMotionCallback onFinishedMotionHandler = NULL, Csm::ACubismMotion::BeganMotionCallback onBeganMotionHandler = NULL);

    /**
     * @brief   引数で指定した表情モーションをセットする
     *
     * @param   expressionID    表情モーションのID
     * @return                  表情が見つかり表情マネージャーへ送出された場合はtrue。
     *                          IDが存在しない場合（従来の静黙無視）はfalse
     */
    bool SetExpression(const Csm::csmChar* expressionID);

    /**
     * @brief   ランダムに選ばれた表情モーションをセットする
     *
     */
    void SetRandomExpression();

    /**
     * @brief   すべてのモーションを停止する
     *
     */
    void StopAllMotions();

    /**
    * @brief   イベントの発火を受け取る
    *
    */
    virtual void MotionEventFired(const Live2D::Cubism::Framework::csmString& eventValue);

    /**
     * @brief    当たり判定テスト。<br>
     *            指定IDの頂点リストから矩形を計算し、座標が矩形範囲内か判定する。
     *
     * @param[in]   hitAreaName     当たり判定をテストする対象のID
     * @param[in]   x               判定を行うX座標
     * @param[in]   y               判定を行うY座標
     */
    virtual Csm::csmBool HitTest(const Csm::csmChar* hitAreaName, Csm::csmFloat32 x, Csm::csmFloat32 y);

    std::vector<std::string> GetHitAreaNames() const;

    void SetUserLayout(float offsetX, float offsetY, float scale);
    void AdjustUserOffset(float dx, float dy);
    void AdjustUserScale(float factor);
    void ResetUserLayout();

    float GetUserOffsetX() const { return _userOffsetX; }
    float GetUserOffsetY() const { return _userOffsetY; }
    float GetUserScale() const { return _userScale; }

protected:
    /**
     *  @brief  モデルを描画する処理。モデルを描画する空間のView-Projection行列を渡す。
     *
     */
    void DoDraw();

private:
    /**
     * @brief model3.jsonからモデルを生成する。<br>
     *         model3.jsonの記述に従ってモデル生成、モーション、物理演算などのコンポーネント生成を行う。
     *
     * @param[in]   setting     ICubismModelSettingのインスタンス
     *
     */
    void SetupModel(Csm::ICubismModelSetting* setting);

    /**
     * @brief OpenGLのテクスチャユニットにテクスチャをロードする
     *
     */
    void SetupTextures();

    /**
     * @brief   モーションデータをグループ名から一括でロードする。<br>
     *           モーションデータの名前は内部でModelSettingから取得する。
     *
     * @param[in]   group  モーションデータのグループ名
     */
    void PreloadMotionGroup(const Csm::csmChar* group);

    /**
     * @brief   モーションデータをグループ名から一括で解放する。<br>
     *           モーションデータの名前は内部でModelSettingから取得する。
     *
     * @param[in]   group  モーションデータのグループ名
     */
    void ReleaseMotionGroup(const Csm::csmChar* group) const;

    /**
    * @brief すべてのモーションデータの解放
    *
    * すべてのモーションデータを解放する。
    */
    void ReleaseMotions();

    /**
    * @brief すべての表情データの解放
    *
    * すべての表情データを解放する。
    */
    void ReleaseExpressions();

    Csm::ICubismModelSetting* _modelSetting; ///< モデルセッティング情報
    Csm::csmString _modelHomeDir; ///< モデルセッティングが置かれたディレクトリ
    Csm::csmFloat32 _userTimeSeconds; ///< デルタ時間の積算値[秒]
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds; ///< モデルに設定されたまばたき機能用パラメータID
    Csm::csmVector<Csm::CubismIdHandle> _lipSyncIds; ///< モデルに設定されたリップシンク機能用パラメータID
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*>   _motions; ///< 読み込まれているモーションのリスト
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*>   _expressions; ///< 読み込まれている表情のリスト
    Csm::csmVector<Csm::csmRectF> _hitArea;
    Csm::csmVector<Csm::csmRectF> _userArea;
    const Csm::CubismId* _idParamAngleX; ///< パラメータID: ParamAngleX
    const Csm::CubismId* _idParamAngleY; ///< パラメータID: ParamAngleX
    const Csm::CubismId* _idParamAngleZ; ///< パラメータID: ParamAngleX
    const Csm::CubismId* _idParamBodyAngleX; ///< パラメータID: ParamBodyAngleX
    const Csm::CubismId* _idParamEyeBallX; ///< パラメータID: ParamEyeBallX
    const Csm::CubismId* _idParamEyeBallY; ///< パラメータID: ParamEyeBallXY

    // Cubism SDK 5-r.5 replacements for removed CubismUserModel members:
    // - _dragX/_dragY -> CubismLook + CubismLookUpdater registered in the
    //   base-class CubismUpdateScheduler during SetupModel (the scheduler
    //   owns and deletes the updater instances)
    // - _lipSync      -> local gate for the dual-source lip sync block
    //   (voice-pack timeline Set / wav RMS Add; the official single-source
    //   CubismLipSyncUpdater only supports the Add(0.8) style)
    bool _lipSyncEnabled = true; ///< リップシンク適用の有効フラグ

    LAppWavFileHandler_Common _wavFileHandler; ///< wavファイルハンドラ

    struct LipSyncDataPoint {
        Csm::csmFloat32 time;
        Csm::csmFloat32 value;
    };
    Csm::csmVector<LipSyncDataPoint> _lipSyncData;
    Csm::csmFloat32 _lipSyncDataElapsed = 0.0f;
    bool _lipSyncDataActive = false;

public:
    void StartLipSyncFromFile(const std::string& path);
    void StopLipSyncData();

private:

    // Identity of the most recent protocol-started motion, consumed by
    // GetMotionProgress() for the motion_progress event (events.md §17).
    // Set on every successful StartMotion / StartMotionFromFile; the handle
    // goes stale (= lookup misses) the moment the entry finishes or is
    // erased from the queue, which is exactly the "stop on motion end"
    // contract — motion_finished already covers the end itself.
    struct MotionProgressId {
        Csm::CubismMotionQueueEntryHandle handle = nullptr;
        bool isPathVariant = false;
        std::string group;
        int index = -1;
        std::string path;
    } _progressMotion;

    float _userOffsetX = 0.0f;
    float _userOffsetY = 0.0f;
    float _userScale   = 1.0f;
};
