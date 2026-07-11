#ifdef USE_VULKAN

#include "VulkanBackend.hpp"
#include "LAppPal.hpp"
#include <GLFW/glfw3.h>
#include <vector>
#include <cstring>
#include <set>
#include <algorithm>

#include <cstdio>

using namespace Csm;

static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// --- Debug callback (port from Demo, replace std::cout with LAppPal::PrintLogLn) ---

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    LAppPal::PrintLogLn("[Vulkan] validation layer: %s", pCallbackData->pMessage);
    return VK_FALSE;
}

// --- Extension function proxies ---

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

// --- Private helper methods ---

bool VulkanBackend::CheckValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : VALIDATION_LAYERS)
    {
        bool layerFound = false;

        for (uint32_t i = 0; i < availableLayers.size(); i++)
        {
            if (strcmp(layerName, availableLayers[i].layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound)
        {
            return false;
        }
    }

    return true;
}

std::vector<const char*> VulkanBackend::GetRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < glfwExtensionCount; i++)
    {
        extensions.push_back(glfwExtensions[i]);
    }

    if (_enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

void VulkanBackend::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;
}

void VulkanBackend::CreateInstance()
{
    if (_enableValidationLayers && !CheckValidationLayerSupport())
    {
        LAppPal::PrintLogLn("[VulkanBackend] validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "desktop-pet-renderer";
    appInfo.pEngineName = "desktop-pet-renderer";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    std::vector<const char*> extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]));
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
        PopulateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to create instance!");
    }
}

void VulkanBackend::SetupDebugMessenger()
{
    if (!_enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    PopulateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(_instance, &createInfo, nullptr, &_debugMessenger) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to set up debug messenger!");
    }
}

void VulkanBackend::CreateSurface()
{
    if (glfwCreateWindowSurface(_instance, _window, nullptr, &_surface) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to create window surface!");
    }
}

VulkanBackend::QueueFamilyIndices VulkanBackend::FindQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilies.size(); i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);
        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.isComplete())
        {
            break;
        }
    }

    _queueFamilyIndices = indices;
    return indices;
}

// BUG FIX: Use strcmp for full string comparison instead of *a == *b (first char only)
bool VulkanBackend::CheckDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    for (uint32_t i = 0; i < sizeof(DEVICE_EXTENSIONS) / sizeof(DEVICE_EXTENSIONS[0]); i++)
    {
        bool found = false;
        for (uint32_t j = 0; j < availableExtensions.size(); j++)
        {
            if (strcmp(DEVICE_EXTENSIONS[i], availableExtensions[j].extensionName) == 0)
            {
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

bool VulkanBackend::IsDeviceSuitable(VkPhysicalDevice device)
{
    FindQueueFamilies(device);
    bool extensionsSupported = CheckDeviceExtensionSupport(device);
    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(device);
        swapChainAdequate = !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return _queueFamilyIndices.isComplete() && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

void VulkanBackend::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

    for (uint32_t i = 0; i < devices.size(); i++)
    {
        if (IsDeviceSuitable(devices[i]))
        {
            _physicalDevice = devices[i];
            break;
        }
    }

    if (_physicalDevice == VK_NULL_HANDLE)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to find a suitable GPU!");
    }
}

void VulkanBackend::CreateLogicalDevice()
{
    FindQueueFamilies(_physicalDevice);

    std::set<uint32_t> uniqueQueueFamilies;
    uniqueQueueFamilies.insert(_queueFamilyIndices.graphicsFamily);
    uniqueQueueFamilies.insert(_queueFamilyIndices.presentFamily);

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = sizeof(DEVICE_EXTENSIONS) / sizeof(DEVICE_EXTENSIONS[0]);
    createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS;

    if (_enableValidationLayers)
    {
        createInfo.enabledLayerCount = sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);
        createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    // pNext feature chain: deviceFeatures2 → vulkan13Features → extendedDynamicState
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicState{};
    extendedDynamicState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    extendedDynamicState.extendedDynamicState = VK_TRUE;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.synchronization2 = VK_TRUE;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.pNext = &extendedDynamicState;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &vulkan13Features;
    vkGetPhysicalDeviceFeatures2(_physicalDevice, &deviceFeatures2);
    createInfo.pNext = &deviceFeatures2;

    if (vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to create logical device!");
    }

    vkGetDeviceQueue(_device, _queueFamilyIndices.graphicsFamily, 0, &_graphicQueue);
    vkGetDeviceQueue(_device, _queueFamilyIndices.presentFamily, 0, &_presentQueue);
}

void VulkanBackend::ChooseSupportedDepthFormat()
{
    VkFormat depthFormats[5] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (int32_t i = 0; i < sizeof(depthFormats) / sizeof(depthFormats[0]); i++)
    {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(_physicalDevice, depthFormats[i], &formatProps);

        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            _depthFormat = depthFormats[i];
            return;
        }
    }
    LAppPal::PrintLogLn("[VulkanBackend] can't find depth format!");
    _depthFormat = depthFormats[0];
}

// --- Swapchain helpers (merged from SwapchainManager) ---

VulkanBackend::SwapchainSupportDetails VulkanBackend::QuerySwapchainSupport(VkPhysicalDevice device)
{
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanBackend::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (uint32_t i = 0; i < availableFormats.size(); i++)
    {
        if (availableFormats[i].format == SURFACE_FORMAT && availableFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormats[i];
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanBackend::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (uint32_t i = 0; i < availablePresentModes.size(); i++)
    {
        if (availablePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentModes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanBackend::ChooseSwapExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }
    else
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::max(capabilities.minImageExtent.width,
            std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height,
            std::min(capabilities.maxImageExtent.height, actualExtent.height));

        return actualExtent;
    }
}

void VulkanBackend::CreateSwapchain()
{
    SwapchainSupportDetails swapChainSupport = QuerySwapchainSupport(_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    _swapchainExtent = ChooseSwapExtent(_window, swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = _surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = _swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (_queueFamilyIndices.graphicsFamily != _queueFamilyIndices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        uint32_t queueFamilyIndices[2] = {_queueFamilyIndices.graphicsFamily, _queueFamilyIndices.presentFamily};
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (swapChainSupport.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
        compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    else if (swapChainSupport.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
        compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    createInfo.compositeAlpha = compositeAlpha;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchain) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to create swap chain");
    }

    vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, nullptr);
    _swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, _swapchainImages.data());

    _swapchainImageCount = imageCount;
    _swapchainImageFormat = surfaceFormat.format;

    _swapchainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = _swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = _swapchainImageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(_device, &viewInfo, nullptr, &_swapchainImageViews[i]) != VK_SUCCESS)
        {
            LAppPal::PrintLogLn("[VulkanBackend] failed to create image view");
        }
    }
}

void VulkanBackend::TransitionSwapchainLayouts()
{
    for (size_t i = 0; i < _swapchainImages.size(); i++)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.image = _swapchainImages[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = _commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(_device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(_graphicQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_graphicQueue);
        vkFreeCommandBuffers(_device, _commandPool, 1, &commandBuffer);
    }
}

void VulkanBackend::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = _queueFamilyIndices.graphicsFamily;

    if (vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to create graphics command pool!");
    }
}

void VulkanBackend::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_imageAvailableSemaphore) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to create semaphore!");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(_device, &fenceInfo, nullptr, &_inFlightFence) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to create in-flight fence!");
    }
}

// --- Public IGraphicsBackend methods ---

bool VulkanBackend::InitializeGraphics(GLFWwindow* window)
{
    _window = window;
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    if (_physicalDevice == VK_NULL_HANDLE)
    {
        LAppPal::PrintLogLn("[VulkanBackend] No suitable GPU found, initialization failed");
        return false;
    }
    CreateLogicalDevice();
    ChooseSupportedDepthFormat();
    CreateSwapchain();
    CreateCommandPool();
    CreateReadbackBuffer();
    if (_readbackBuffer == VK_NULL_HANDLE)
    {
        LAppPal::PrintLogLn("[VulkanBackend] Failed to create readback buffer, initialization failed");
        return false;
    }
    TransitionSwapchainLayouts();
    CreateSyncObjects();
    LAppPal::PrintLogLn("[VulkanBackend] Initialized successfully");
    return true;
}

void VulkanBackend::BeginFrame(int width, int height)
{
    // Cubism SDK Vulkan renderer submits commands via SubmitCommand() with
    // vkQueueWaitIdle() synchronization, so our fence is never signaled by
    // the rendering path. Use vkDeviceWaitIdle to ensure prior work completes
    // before acquiring the next swapchain image.
    vkDeviceWaitIdle(_device);

    VkResult result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
        VK_NULL_HANDLE, VK_NULL_HANDLE, &_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        _isSwapchainInvalid = true;
        return;
    }
    else if (result == VK_SUBOPTIMAL_KHR)
    {
        _isSwapchainInvalid = true;
    }
    else if (result != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to acquire swap chain image!");
    }
}

void VulkanBackend::EndFrame(GLFWwindow* window)
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    VkSwapchainKHR swapChains[] = {_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &_imageIndex;

    VkResult result = vkQueuePresentKHR(_presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _framebufferResized)
    {
        _framebufferResized = false;
        _isSwapchainInvalid = true;
    }
    else if (result != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to present swap chain image!");
    }
}

void VulkanBackend::QueuePresent()
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    VkSwapchainKHR swapChains[] = {_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &_imageIndex;

    VkResult result = vkQueuePresentKHR(_presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _framebufferResized)
    {
        _framebufferResized = false;
        _isSwapchainInvalid = true;
    }
    else if (result != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] failed to present swap chain image!");
    }
}

uint32_t VulkanBackend::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    LAppPal::PrintLogLn("[VulkanBackend] Failed to find suitable memory type");
    return UINT32_MAX;
}

void VulkanBackend::CreateReadbackBuffer()
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(_device, &bufferInfo, nullptr, &_readbackBuffer) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] Failed to create readback buffer");
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(_device, _readbackBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(_device, &allocInfo, nullptr, &_readbackBufferMemory) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] Failed to allocate readback buffer memory");
        vkDestroyBuffer(_device, _readbackBuffer, nullptr);
        _readbackBuffer = VK_NULL_HANDLE;
        return;
    }

    vkBindBufferMemory(_device, _readbackBuffer, _readbackBufferMemory, 0);
}

void VulkanBackend::DestroyReadbackBuffer()
{
    if (_readbackBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(_device, _readbackBuffer, nullptr);
        _readbackBuffer = VK_NULL_HANDLE;
    }
    if (_readbackBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(_device, _readbackBufferMemory, nullptr);
        _readbackBufferMemory = VK_NULL_HANDLE;
    }
}

// --- Command helpers ---

VkCommandBuffer VulkanBackend::BeginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = _commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanBackend::SubmitCommand(VkCommandBuffer commandBuffer, bool isFirstDraw)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (isFirstDraw)
    {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &_imageAvailableSemaphore;
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.pWaitDstStageMask = waitStages;
    }
    else
    {
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT };
        submitInfo.pWaitDstStageMask = waitStages;
    }

    vkQueueSubmit(_graphicQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(_graphicQueue);
    vkFreeCommandBuffers(_device, _commandPool, 1, &commandBuffer);
}

// --- Swapchain management ---

void VulkanBackend::CleanupSwapchain()
{
    if (_swapchain != VK_NULL_HANDLE)
    {
        for (size_t i = 0; i < _swapchainImageViews.size(); i++)
        {
            vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
        }
        _swapchainImageViews.clear();
        _swapchainImages.clear();
        vkDestroySwapchainKHR(_device, _swapchain, nullptr);
        _swapchain = VK_NULL_HANDLE; // BUG FIX: reset handle after cleanup
    }
}

void VulkanBackend::RecreateSwapchain()
{
    vkDeviceWaitIdle(_device);
    CleanupSwapchain();
    CreateSwapchain();
    TransitionSwapchainLayouts();
}

void VulkanBackend::ReleaseGraphics()
{
    vkDeviceWaitIdle(_device); // BUG FIX: first line must be vkDeviceWaitIdle

    CleanupSwapchain();
    DestroyReadbackBuffer();

    for (auto& [id, tex] : _textureMap)
    {
        if (tex.imageView != VK_NULL_HANDLE) vkDestroyImageView(_device, tex.imageView, nullptr);
        if (tex.image != VK_NULL_HANDLE) vkDestroyImage(_device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE) vkFreeMemory(_device, tex.memory, nullptr);
    }
    _textureMap.clear();

    vkDestroyFence(_device, _inFlightFence, nullptr);
    vkDestroySemaphore(_device, _imageAvailableSemaphore, nullptr);

    if (_enableValidationLayers)
    {
        DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
    }

    vkDestroyCommandPool(_device, _commandPool, nullptr);
    vkDestroyDevice(_device, nullptr);
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    vkDestroyInstance(_instance, nullptr);

    LAppPal::PrintLogLn("[VulkanBackend] Vulkan resources released");
}

// --- Accessors (updated for std::vector) ---

VkImage VulkanBackend::GetSwapchainImage() const
{
    if (_imageIndex < _swapchainImages.size())
        return _swapchainImages[_imageIndex];
    return VK_NULL_HANDLE;
}

VkImageView VulkanBackend::GetSwapchainImageView() const
{
    if (_imageIndex < _swapchainImageViews.size())
        return _swapchainImageViews[_imageIndex];
    return VK_NULL_HANDLE;
}

uint64_t VulkanBackend::CreateTexture(const void* data, int width, int height, int channels)
{
    if (data == nullptr || width <= 0 || height <= 0)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: invalid input (data=%p w=%d h=%d)",
            data, width, height);
        return 0;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    // --- Create VkImage ---
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    TextureData tex{};
    if (vkCreateImage(_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: failed to create VkImage");
        return 0;
    }

    // --- Allocate + bind device-local memory ---
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(_device, tex.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (allocInfo.memoryTypeIndex == UINT32_MAX)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: no suitable device-local memory type");
        vkDestroyImage(_device, tex.image, nullptr);
        return 0;
    }

    if (vkAllocateMemory(_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: failed to allocate image memory");
        vkDestroyImage(_device, tex.image, nullptr);
        return 0;
    }
    vkBindImageMemory(_device, tex.image, tex.memory, 0);

    // --- Create staging buffer (host-visible + host-coherent) ---
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(_device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: failed to create staging buffer");
        vkFreeMemory(_device, tex.memory, nullptr);
        vkDestroyImage(_device, tex.image, nullptr);
        return 0;
    }

    VkMemoryRequirements stagingReq;
    vkGetBufferMemoryRequirements(_device, stagingBuffer, &stagingReq);

    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingReq.size;
    stagingAlloc.memoryTypeIndex = FindMemoryType(stagingReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (stagingAlloc.memoryTypeIndex == UINT32_MAX)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: no suitable host-visible memory type");
        vkDestroyBuffer(_device, stagingBuffer, nullptr);
        vkFreeMemory(_device, tex.memory, nullptr);
        vkDestroyImage(_device, tex.image, nullptr);
        return 0;
    }

    if (vkAllocateMemory(_device, &stagingAlloc, nullptr, &stagingMemory) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: failed to allocate staging memory");
        vkDestroyBuffer(_device, stagingBuffer, nullptr);
        vkFreeMemory(_device, tex.memory, nullptr);
        vkDestroyImage(_device, tex.image, nullptr);
        return 0;
    }
    vkBindBufferMemory(_device, stagingBuffer, stagingMemory, 0);

    // --- Map + copy pixel data into staging buffer ---
    void* mappedData = nullptr;
    if (vkMapMemory(_device, stagingMemory, 0, imageSize, 0, &mappedData) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: failed to map staging memory");
        vkFreeMemory(_device, stagingMemory, nullptr);
        vkDestroyBuffer(_device, stagingBuffer, nullptr);
        vkFreeMemory(_device, tex.memory, nullptr);
        vkDestroyImage(_device, tex.image, nullptr);
        return 0;
    }
    std::memcpy(mappedData, data, static_cast<size_t>(imageSize));
    vkUnmapMemory(_device, stagingMemory);

    // --- Record upload commands ---
    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();

    // Layout transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier layoutToDst{};
    layoutToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    layoutToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    layoutToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    layoutToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    layoutToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    layoutToDst.image = tex.image;
    layoutToDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    layoutToDst.subresourceRange.baseMipLevel = 0;
    layoutToDst.subresourceRange.levelCount = 1;
    layoutToDst.subresourceRange.baseArrayLayer = 0;
    layoutToDst.subresourceRange.layerCount = 1;
    layoutToDst.srcAccessMask = 0;
    layoutToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &layoutToDst);

    // Copy staging buffer -> image
    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(cmdBuf, stagingBuffer, tex.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // Layout transition: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    VkImageMemoryBarrier layoutToShader{};
    layoutToShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    layoutToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    layoutToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    layoutToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    layoutToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    layoutToShader.image = tex.image;
    layoutToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    layoutToShader.subresourceRange.baseMipLevel = 0;
    layoutToShader.subresourceRange.levelCount = 1;
    layoutToShader.subresourceRange.baseArrayLayer = 0;
    layoutToShader.subresourceRange.layerCount = 1;
    layoutToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    layoutToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &layoutToShader);

    SubmitCommand(cmdBuf); // internally calls vkQueueWaitIdle

    // --- Cleanup staging buffer (image memory is now device-local) ---
    vkDestroyBuffer(_device, stagingBuffer, nullptr);
    vkFreeMemory(_device, stagingMemory, nullptr);

    // --- Create VkImageView ---
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device, &viewInfo, nullptr, &tex.imageView) != VK_SUCCESS)
    {
        LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: failed to create VkImageView");
        vkFreeMemory(_device, tex.memory, nullptr);
        vkDestroyImage(_device, tex.image, nullptr);
        return 0;
    }

    // --- Register in texture map, return handle ---
    uint64_t handle = _nextTextureId++;
    _textureMap[handle] = tex;

    LAppPal::PrintLogLn("[VulkanBackend] CreateTexture: %dx%d handle=%llu",
        width, height, static_cast<unsigned long long>(handle));
    return handle;
}

void VulkanBackend::DeleteTexture(uint64_t handle)
{
    auto it = _textureMap.find(handle);
    if (it == _textureMap.end())
    {
        return;
    }

    const TextureData& tex = it->second;
    if (tex.imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(_device, tex.imageView, nullptr);
    }
    if (tex.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(_device, tex.image, nullptr);
    }
    if (tex.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(_device, tex.memory, nullptr);
    }
    _textureMap.erase(it);
}

VkImageView VulkanBackend::GetTextureImageView(uint64_t handle) const
{
    auto it = _textureMap.find(handle);
    if (it != _textureMap.end())
    {
        return it->second.imageView;
    }
    return VK_NULL_HANDLE;
}

bool VulkanBackend::IsPixelTransparent(int x, int y, int windowHeight)
{
    VkImage swapchainImage = GetSwapchainImage();
    if (swapchainImage == VK_NULL_HANDLE || _readbackBuffer == VK_NULL_HANDLE)
    {
        return true;
    }

    VkCommandBuffer cmdBuf = BeginSingleTimeCommands();

    VkImageMemoryBarrier preCopyBarrier{};
    preCopyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    preCopyBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    preCopyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    preCopyBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preCopyBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preCopyBarrier.image = swapchainImage;
    preCopyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    preCopyBarrier.subresourceRange.baseMipLevel = 0;
    preCopyBarrier.subresourceRange.levelCount = 1;
    preCopyBarrier.subresourceRange.baseArrayLayer = 0;
    preCopyBarrier.subresourceRange.layerCount = 1;
    preCopyBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    preCopyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &preCopyBarrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x, y, 0};
    region.imageExtent = {1, 1, 1};

    vkCmdCopyImageToBuffer(cmdBuf, swapchainImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        _readbackBuffer, 1, &region);

    VkImageMemoryBarrier postCopyBarrier = preCopyBarrier;
    postCopyBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    postCopyBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    postCopyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    postCopyBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        0, nullptr, 0, nullptr, 1, &postCopyBarrier);

    SubmitCommand(cmdBuf);

    uint8_t* data = nullptr;
    vkMapMemory(_device, _readbackBufferMemory, 0, 4, 0, reinterpret_cast<void**>(&data));
    uint8_t alpha = 0;
    if (data)
    {
        alpha = data[3];
        vkUnmapMemory(_device, _readbackBufferMemory);
    }

    return (alpha == 0);
}

#endif // USE_VULKAN
