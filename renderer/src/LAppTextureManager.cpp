/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppTextureManager.hpp"
#include <iostream>
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "LAppPal.hpp"

#ifdef USE_VULKAN
#include "graphics/VulkanBackend.hpp"
#include <cmath>
#endif

LAppTextureManager::LAppTextureManager() : LAppTextureManager_Common()
{
#ifdef USE_VULKAN
    _sequenceId = 0;
#endif
}

LAppTextureManager::~LAppTextureManager()
{
    ReleaseTextures();
}

LAppTextureManager::TextureInfo* LAppTextureManager::CreateTextureFromPngFile(std::string fileName)
{
    //search loaded texture already.
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->fileName == fileName)
        {
            return _texturesInfo[i];
        }
    }

    uint64_t textureId = 0;
    int width, height, channels;
    unsigned int size;
    unsigned char* png;
    unsigned char* address;

    address = LAppPal::LoadFileAsBytes(fileName, &size);

    // png情報を取得する
    png = stbi_load_from_memory(
        address,
        static_cast<int>(size),
        &width,
        &height,
        &channels,
        STBI_rgb_alpha);
    {

#ifdef PREMULTIPLIED_ALPHA_ENABLE
        unsigned int* fourBytes = reinterpret_cast<unsigned int*>(png);
        for (int i = 0; i < width * height; i++)
        {
            unsigned char* p = png + i * 4;
            fourBytes[i] = LAppTextureManager_Common::Premultiply(p[0], p[1], p[2], p[3]);
        }
#endif
    }

    textureId = _backend->CreateTexture(png, width, height, 4);

    // 解放処理
    stbi_image_free(png);
    LAppPal::ReleaseBytes(address);

    LAppTextureManager::TextureInfo* textureInfo = new LAppTextureManager::TextureInfo();
    if (textureInfo != NULL)
    {
        textureInfo->fileName = fileName;
        textureInfo->width = width;
        textureInfo->height = height;
        textureInfo->id = textureId;

        _texturesInfo.PushBack(textureInfo);
    }

    return textureInfo;

}

#ifdef USE_VULKAN
LAppTextureManager::TextureInfo* LAppTextureManager::CreateTextureFromPngFile(
    std::string fileName, VkFormat format, VkImageTiling tiling,
    VkImageUsageFlags usage, VkMemoryPropertyFlags imageProperties,
    Csm::csmFloat32 anisotropy)
{
    // Cache check — reuse existing texture
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->fileName == fileName)
        {
            return _texturesInfo[i];
        }
    }

    // Load PNG file
    unsigned int size;
    unsigned char* address = LAppPal::LoadFileAsBytes(fileName, &size);
    if (!address)
    {
        LAppPal::PrintLogLn("[LAppTextureManager] Failed to load file: %s", fileName.c_str());
        return nullptr;
    }

    int width, height, channels;
    unsigned char* png = stbi_load_from_memory(address, static_cast<int>(size),
        &width, &height, &channels, STBI_rgb_alpha);
    LAppPal::ReleaseBytes(address);

    if (!png)
    {
        LAppPal::PrintLogLn("[LAppTextureManager] stbi_load_from_memory failed: %s", fileName.c_str());
        return nullptr;
    }

#ifdef PREMULTIPLIED_ALPHA_ENABLE
    unsigned int* fourBytes = reinterpret_cast<unsigned int*>(png);
    for (int i = 0; i < width * height; i++)
    {
        unsigned char* p = png + i * 4;
        fourBytes[i] = LAppTextureManager_Common::Premultiply(p[0], p[1], p[2], p[3]);
    }
#endif

    // Get Vulkan device
    auto* vkBackend = static_cast<VulkanBackend*>(_backend);
    VkDevice device = vkBackend->GetDevice();
    VkPhysicalDevice physicalDevice = vkBackend->GetPhysicalDevice();

    // Create staging buffer and upload pixel data
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * 4);
    Live2D::Cubism::Framework::CubismBufferVulkan stagingBuffer;
    stagingBuffer.CreateBuffer(device, physicalDevice, imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.Map(device, imageSize);
    stagingBuffer.MemCpy(png, imageSize);
    stagingBuffer.UnMap(device);
    stbi_image_free(png);

    // Calculate mip levels
    _mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    // Create VkImage
    Live2D::Cubism::Framework::CubismImageVulkan textureImage;
    textureImage.CreateImage(device, physicalDevice, width, height, _mipLevels,
        format, tiling, usage);

    // Layout transition + copy staging → image
    VkCommandBuffer cmdBuf = vkBackend->BeginSingleTimeCommands();
    textureImage.SetImageLayout(cmdBuf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        _mipLevels, VK_IMAGE_ASPECT_COLOR_BIT);
    CopyBufferToImage(cmdBuf, stagingBuffer.GetBuffer(), textureImage.GetImage(),
        width, height);
    vkBackend->SubmitCommand(cmdBuf);

    // Generate mipmap chain
    GenerateMipmaps(textureImage, width, height, _mipLevels);

    // Create ImageView + Sampler
    textureImage.CreateView(device, format, VK_IMAGE_ASPECT_COLOR_BIT, _mipLevels);
    textureImage.CreateSampler(device, anisotropy, _mipLevels);
    _textures.PushBack(textureImage);

    // Create TextureInfo
    _sequenceId++;
    LAppTextureManager::TextureInfo* textureInfo = new LAppTextureManager::TextureInfo();
    textureInfo->fileName = fileName;
    textureInfo->width = width;
    textureInfo->height = height;
    textureInfo->id = _sequenceId;
    _texturesInfo.PushBack(textureInfo);

    // Cleanup staging buffer
    stagingBuffer.Destroy(device);

    return textureInfo;
}
#endif

#ifdef USE_VULKAN
void LAppTextureManager::CopyBufferToImage(VkCommandBuffer commandBuffer,
    const VkBuffer& buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}
#endif

#ifdef USE_VULKAN
void LAppTextureManager::GenerateMipmaps(
    Live2D::Cubism::Framework::CubismImageVulkan image,
    uint32_t texWidth, uint32_t texHeight, uint32_t mipLevels)
{
    auto* vkBackend = static_cast<VulkanBackend*>(_backend);
    VkCommandBuffer commandBuffer = vkBackend->BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image.GetImage();
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
            image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &barrier);

    vkBackend->SubmitCommand(commandBuffer);
}
#endif

#ifdef USE_VULKAN
bool LAppTextureManager::GetTexture(Csm::csmUint32 textureId,
    Live2D::Cubism::Framework::CubismImageVulkan& retTexture) const
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->id == textureId)
        {
            retTexture = _textures[i];
            return true;
        }
    }
    return false;
}
#endif

void LAppTextureManager::ReleaseTextures()
{
#ifdef USE_VULKAN
    auto* vkBackend = static_cast<VulkanBackend*>(_backend);
    VkDevice device = vkBackend->GetDevice();
    for (Csm::csmUint32 i = 0; i < _textures.GetSize(); i++)
    {
        _textures[i].Destroy(device);
    }
    _textures.Clear();
#else
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        _backend->DeleteTexture(_texturesInfo[i]->id);
    }
#endif
    ReleaseTexturesInfo();
}

void LAppTextureManager::ReleaseTexture(uint64_t textureId)
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->id != textureId)
        {
            continue;
        }
#ifdef USE_VULKAN
        auto* vkBackend = static_cast<VulkanBackend*>(_backend);
        _textures[i].Destroy(vkBackend->GetDevice());
        _textures.Remove(i);
#else
        _backend->DeleteTexture(_texturesInfo[i]->id);
#endif
        delete _texturesInfo[i];
        _texturesInfo.Remove(i);
        break;
    }
}

void LAppTextureManager::ReleaseTexture(std::string fileName)
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->fileName != fileName)
        {
            continue;
        }
#ifdef USE_VULKAN
        auto* vkBackend = static_cast<VulkanBackend*>(_backend);
        _textures[i].Destroy(vkBackend->GetDevice());
        _textures.Remove(i);
#else
        _backend->DeleteTexture(_texturesInfo[i]->id);
#endif
        delete _texturesInfo[i];
        _texturesInfo.Remove(i);
        break;
    }
}
