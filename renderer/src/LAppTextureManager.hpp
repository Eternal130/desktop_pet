/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <string>
#include <cstdint>
#include <Type/csmVector.hpp>

#include "LAppTextureManager_Common.hpp"
#include "graphics/IGraphicsBackend.hpp"

#ifdef USE_VULKAN
#include <vulkan/vulkan.h>
#include <Rendering/Vulkan/CubismClass_Vulkan.hpp>
#include "graphics/VulkanBackend.hpp"
#endif

/**
* @brief テクスチャ管理クラス
*
* 画像読み込み、管理を行うクラス。
*/
class LAppTextureManager : public LAppTextureManager_Common
{
public:
    /**
    * @brief コンストラクタ
    */
    LAppTextureManager();

    /**
    * @brief デストラクタ
    *
    */
    ~LAppTextureManager();

    /**
    * @brief 画像読み込み
    *
    * @param[in] fileName  読み込む画像ファイルパス名
    * @return 画像情報。読み込み失敗時はNULLを返す
    */
    TextureInfo* CreateTextureFromPngFile(std::string fileName);

    /**
    * @brief 画像の解放
    *
    * 配列に存在する画像全てを解放する
    */
    void ReleaseTextures();

    /**
     * @brief 画像の解放
     *
     * 指定したテクスチャIDの画像を解放する
     * @param[in] textureId  解放するテクスチャID
     **/
    void ReleaseTexture(uint64_t textureId);

    void SetGraphicsBackend(IGraphicsBackend* backend) { _backend = backend; }

#ifdef USE_VULKAN
    TextureInfo* CreateTextureFromPngFile(
        std::string fileName,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags imageProperties,
        Csm::csmFloat32 anisotropy
    );

    bool GetTexture(Csm::csmUint32 textureId,
                    Live2D::Cubism::Framework::CubismImageVulkan& retTexture) const;
#endif

    /**
    * @brief 画像の解放
    *
    * 指定した名前の画像を解放する
    * @param[in] fileName  解放する画像ファイルパス名
    **/
    void ReleaseTexture(std::string fileName);

private:
    IGraphicsBackend* _backend = nullptr;

#ifdef USE_VULKAN
    void CopyBufferToImage(VkCommandBuffer commandBuffer, const VkBuffer& buffer,
                           VkImage image, uint32_t width, uint32_t height);
    void GenerateMipmaps(Live2D::Cubism::Framework::CubismImageVulkan image,
                         uint32_t texWidth, uint32_t texHeight, uint32_t mipLevels);
#endif

#ifdef USE_VULKAN
    Csm::csmVector<Live2D::Cubism::Framework::CubismImageVulkan> _textures;
    Csm::csmUint32 _sequenceId = 0;
    uint32_t _mipLevels = 0;
#endif
};
