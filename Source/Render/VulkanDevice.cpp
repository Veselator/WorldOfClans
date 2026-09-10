#include "VulkanDevice.h"
#include "../Platform/Window.h"

#include <algorithm>
#include <cstring>
#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace woc
{
    const char* VulkanResultToString(VkResult result)
    {
        switch (result)
        {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
        default: return "VK_ERROR_UNKNOWN";
        }
    }

    namespace
    {
        constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT /*types*/,
            const VkDebugUtilsMessengerCallbackDataEXT* data,
            void* /*userData*/)
        {
            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                WOC_LOG_ERROR("[vulkan] ", data->pMessage);
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                WOC_LOG_WARN("[vulkan] ", data->pMessage);
            return VK_FALSE;
        }

        bool HasLayer(const char* name)
        {
            u32 count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            return std::any_of(layers.begin(), layers.end(),
                [name](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
        }

        bool HasInstanceExtension(const char* name)
        {
            u32 count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> extensions(count);
            vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
            return std::any_of(extensions.begin(), extensions.end(),
                [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
        }
    }

    void VulkanDevice::Initialise(Window& window, bool enableValidation)
    {
        VK_CHECK(volkInitialize());
        CreateInstance(enableValidation);
        volkLoadInstance(m_instance);
        if (m_validationEnabled) CreateDebugMessenger();
        CreateSurface(window);
        PickPhysicalDevice();
        CreateLogicalDevice(enableValidation);
        volkLoadDevice(m_device);

        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_graphicsFamily;
        VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));
    }

    void VulkanDevice::CreateInstance(bool enableValidation)
    {
        m_validationEnabled = enableValidation && HasLayer(kValidationLayer);
        if (enableValidation && !m_validationEnabled)
        {
            WOC_LOG_WARN("Validation layers requested but not available; continuing without them");
        }

        VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        appInfo.pApplicationName = "World of Clans";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "WoC Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        std::vector<const char*> extensions{ VK_KHR_SURFACE_EXTENSION_NAME };
#ifdef _WIN32
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
        if (m_validationEnabled && HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        else
            m_validationEnabled = false; // the layer is useless without its reporting extension

        std::vector<const char*> layers;
        if (m_validationEnabled) layers.push_back(kValidationLayer);

        VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        info.pApplicationInfo = &appInfo;
        info.enabledExtensionCount = static_cast<u32>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.enabledLayerCount = static_cast<u32>(layers.size());
        info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

        VK_CHECK(vkCreateInstance(&info, nullptr, &m_instance));
    }

    void VulkanDevice::CreateDebugMessenger()
    {
        if (!vkCreateDebugUtilsMessengerEXT) return;

        VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = DebugCallback;
        vkCreateDebugUtilsMessengerEXT(m_instance, &info, nullptr, &m_debugMessenger);
    }

    void VulkanDevice::CreateSurface(Window& window)
    {
#ifdef _WIN32
        VkWin32SurfaceCreateInfoKHR info{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        info.hinstance = reinterpret_cast<HINSTANCE>(window.NativeInstance());
        info.hwnd = reinterpret_cast<HWND>(window.NativeHandle());
        VK_CHECK(vkCreateWin32SurfaceKHR(m_instance, &info, nullptr, &m_surface));
#else
        (void)window;
        throw std::runtime_error("No surface backend for this platform");
#endif
    }

    void VulkanDevice::PickPhysicalDevice()
    {
        u32 count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
        if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

        std::vector<VkPhysicalDevice> devices(count);
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()));

        i32 bestScore = -1;
        for (VkPhysicalDevice candidate : devices)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate, &props);

            u32 familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

            u32 graphics = UINT32_MAX, present = UINT32_MAX;
            for (u32 i = 0; i < familyCount; ++i)
            {
                if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = std::min(graphics, i);
                VkBool32 supported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_surface, &supported);
                if (supported && present == UINT32_MAX) present = i;
            }
            if (graphics == UINT32_MAX || present == UINT32_MAX) continue;

            // Discrete GPUs win; anything that can present is acceptable as a fallback.
            i32 score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 100;
            score += static_cast<i32>(props.limits.maxImageDimension2D / 64);

            if (score > bestScore)
            {
                bestScore = score;
                m_physicalDevice = candidate;
                m_graphicsFamily = graphics;
                m_presentFamily = present;
                m_properties = props;
                m_deviceName = props.deviceName;
            }
        }

        if (m_physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("No GPU supports both graphics and presentation");

        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
        WOC_LOG_INFO("GPU: ", m_deviceName, " (graphics family ", m_graphicsFamily,
                     ", present family ", m_presentFamily, ")");
    }

    void VulkanDevice::CreateLogicalDevice(bool enableValidation)
    {
        const std::set<u32> uniqueFamilies{ m_graphicsFamily, m_presentFamily };
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        const f32 priority = 1.0f;
        for (u32 family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            qi.queueFamilyIndex = family;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            queueInfos.push_back(qi);
        }

        VkPhysicalDeviceFeatures available{};
        vkGetPhysicalDeviceFeatures(m_physicalDevice, &available);

        VkPhysicalDeviceFeatures features{};
        features.samplerAnisotropy = available.samplerAnisotropy;
        features.fillModeNonSolid = available.fillModeNonSolid;
        features.wideLines = available.wideLines;

        const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        std::vector<const char*> layers;
        if (enableValidation && m_validationEnabled) layers.push_back(kValidationLayer);

        VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        info.queueCreateInfoCount = static_cast<u32>(queueInfos.size());
        info.pQueueCreateInfos = queueInfos.data();
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = deviceExtensions;
        info.pEnabledFeatures = &features;
        info.enabledLayerCount = static_cast<u32>(layers.size());
        info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

        VK_CHECK(vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device));
        vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
    }

    u32 VulkanDevice::FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const
    {
        for (u32 i = 0; i < m_memoryProperties.memoryTypeCount; ++i)
        {
            const bool typeAllowed = (typeBits & (1u << i)) != 0;
            const bool propsMatch = (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
            if (typeAllowed && propsMatch) return i;
        }
        throw std::runtime_error("No suitable Vulkan memory type");
    }

    VkFormat VulkanDevice::FindDepthFormat() const
    {
        const VkFormat candidates[] = {
            VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT
        };
        for (VkFormat format : candidates)
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return format;
        }
        throw std::runtime_error("No supported depth format");
    }

    VkCommandBuffer VulkanDevice::BeginSingleUse() const
    {
        VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        return commandBuffer;
    }

    void VulkanDevice::EndSingleUse(VkCommandBuffer commandBuffer) const
    {
        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffer;
        VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
    }

    void VulkanDevice::WaitIdle() const
    {
        if (m_device) vkDeviceWaitIdle(m_device);
    }

    void VulkanDevice::Shutdown()
    {
        if (!m_device) return;
        vkDeviceWaitIdle(m_device);

        if (m_commandPool) { vkDestroyCommandPool(m_device, m_commandPool, nullptr); m_commandPool = VK_NULL_HANDLE; }
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;

        if (m_surface) { vkDestroySurfaceKHR(m_instance, m_surface, nullptr); m_surface = VK_NULL_HANDLE; }
        if (m_debugMessenger && vkDestroyDebugUtilsMessengerEXT)
        {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }
        if (m_instance) { vkDestroyInstance(m_instance, nullptr); m_instance = VK_NULL_HANDLE; }
    }
}
