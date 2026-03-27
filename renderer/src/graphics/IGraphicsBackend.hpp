/**
 * @brief 渲染后端抽象接口
 *
 * 定义 OpenGL / Vulkan 双后端的统一接口。
 * 只覆盖 GL/VK 差异化的部分，不覆盖完全相同或完全不同的部分。
 * 由 LAppDelegate 持有，编译期决定实例化哪个后端。
 */

#pragma once

#include <cstdint>

struct GLFWwindow;

class IGraphicsBackend
{
public:
    virtual ~IGraphicsBackend() = default;

    /**
     * @brief 初始化图形上下文
     *
     * OpenGL: glfwMakeContextCurrent + glewInit + GL 状态设置 + PBO 创建
     * Vulkan: CreateInstance + Device + Swapchain + CommandPool
     *
     * @param window  GLFW 窗口句柄
     * @return true 成功, false 失败
     */
    virtual bool InitializeGraphics(GLFWwindow* window) = 0;

    /**
     * @brief 释放图形资源
     */
    virtual void ReleaseGraphics() = 0;

    /**
     * @brief 每帧开始 — 设置视口、清屏
     *
     * OpenGL: glViewport + glClearColor + glClear + glClearDepth
     * Vulkan: AcquireNextImage + BeginCommandBuffer + BeginRenderPass
     *
     * @param width   视口宽度
     * @param height  视口高度
     */
    virtual void BeginFrame(int width, int height) = 0;

    /**
     * @brief 每帧结束 — 交换缓冲区
     *
     * OpenGL: glfwSwapBuffers
     * Vulkan: EndRenderPass + SubmitCommand + Present
     *
     * @param window  GLFW 窗口句柄
     */
    virtual void EndFrame(GLFWwindow* window) = 0;

    /**
     * @brief 创建纹理并上传像素数据
     *
     * OpenGL: glGenTextures + glTexImage2D + glGenerateMipmap + glTexParameteri
     * Vulkan: vkCreateImage + staging buffer + vkCmdCopyBufferToImage
     *
     * @param data      像素数据（RGBA / RGB）
     * @param width     纹理宽度
     * @param height    纹理高度
     * @param channels  通道数（3=RGB, 4=RGBA）
     * @return          不透明纹理句柄（OpenGL: GLuint, Vulkan: 封装后的句柄）
     */
    virtual uint64_t CreateTexture(const void* data, int width, int height, int channels) = 0;

    /**
     * @brief 删除纹理
     *
     * @param handle  CreateTexture 返回的句柄
     */
    virtual void DeleteTexture(uint64_t handle) = 0;

    /**
     * @brief 检测指定像素是否透明（点击穿透检测）
     *
     * OpenGL: PBO 像素回读
     * Vulkan: vkCmdCopyImage to staging buffer → map → read alpha
     *
     * @param x            窗口局部 X 坐标
     * @param y            窗口局部 Y 坐标
     * @param windowHeight 窗口高度（用于 Y 轴翻转）
     * @return true  透明（应穿透）, false  不透明（不穿透）
     */
    virtual bool IsPixelTransparent(int x, int y, int windowHeight) = 0;
};
