#pragma once

#ifdef USE_VULKAN

#include "IGraphicsBackend.hpp"
#include <vulkan/vulkan.h>
#include <vector>

// Merges VulkanManager + SwapchainManager from Cubism SDK Vulkan Demo.
// Implements IGraphicsBackend for LAppDelegate.

class VulkanBackend : public IGraphicsBackend {
public:
    struct QueueFamilyIndices {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        bool isComplete() const {
            return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
        }
    };

    struct SwapchainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    bool InitializeGraphics(GLFWwindow* window) override;
    void ReleaseGraphics() override;
    void BeginFrame(int width, int height) override;
    void EndFrame(GLFWwindow* window) override;
    uint64_t CreateTexture(const void* data, int width, int height, int channels) override;
    void DeleteTexture(uint64_t handle) override;
    bool IsPixelTransparent(int x, int y, int windowHeight) override;

    VkCommandBuffer BeginSingleTimeCommands();
    void SubmitCommand(VkCommandBuffer cmdBuf, bool isFirstDraw = false);

    VkDevice GetDevice() const { return _device; }
    VkPhysicalDevice GetPhysicalDevice() const { return _physicalDevice; }
    VkCommandPool GetCommandPool() const { return _commandPool; }
    VkQueue GetGraphicQueue() const { return _graphicQueue; }
    VkFormat GetDepthFormat() const { return _depthFormat; }
    VkFormat GetImageFormat() const { return VK_FORMAT_R8G8B8A8_UNORM; }

    VkImage GetSwapchainImage() const;
    VkImageView GetSwapchainImageView() const;
    VkExtent2D GetSwapchainExtent() const { return _swapchainExtent; }
    int32_t GetSwapchainImageCount() const { return static_cast<int32_t>(_swapchainImageCount); }
    VkFormat GetSwapchainImageFormat() const { return _swapchainImageFormat; }

    void RecreateSwapchain();
    bool IsSwapchainInvalid() const { return _isSwapchainInvalid; }
    void SetSwapchainInvalid(bool flag) { _isSwapchainInvalid = flag; }
    void SetFrameBufferResized(bool flag) { _framebufferResized = flag; }

private:
    VkInstance _instance = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _graphicQueue = VK_NULL_HANDLE;
    VkQueue _presentQueue = VK_NULL_HANDLE;
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    VkSemaphore _imageAvailableSemaphore = VK_NULL_HANDLE;
    VkFence _inFlightFence = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;
    VkFormat _depthFormat = VK_FORMAT_UNDEFINED;
    uint32_t _imageIndex = 0;

    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent = {0, 0};
    uint32_t _swapchainImageCount = 0;
    VkFormat _swapchainImageFormat = VK_FORMAT_R8G8B8A8_UNORM;

    bool _isSwapchainInvalid = false;
    bool _framebufferResized = false;
    bool _enableValidationLayers = true;

    GLFWwindow* _window = nullptr;

    // Pixel readback (click-through detection)
    VkBuffer _readbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _readbackBufferMemory = VK_NULL_HANDLE;

    QueueFamilyIndices _queueFamilyIndices;

    static constexpr const char* VALIDATION_LAYERS[] = { "VK_LAYER_KHRONOS_validation" };
    static constexpr const char* DEVICE_EXTENSIONS[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME
    };
    static constexpr VkFormat SURFACE_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void ChooseSupportedDepthFormat();
    void CreateCommandPool();
    void CreateSyncObjects();
    void CreateSwapchain();
    void CleanupSwapchain();
    void TransitionSwapchainLayouts();

    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();
    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
    bool IsDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes);
    VkExtent2D ChooseSwapExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities);
    void QueuePresent();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateReadbackBuffer();
    void DestroyReadbackBuffer();
};

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT*, void*);

VkResult CreateDebugUtilsMessengerEXT(VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*,
                                      const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT*);
void DestroyDebugUtilsMessengerEXT(VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks*);

#endif // USE_VULKAN
