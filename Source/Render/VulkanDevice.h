// VulkanDevice.h - instance, physical/logical device, queues and one-shot command helpers.
#pragma once

#include "VulkanCommon.h"
#include "../Core/Singleton.h"

namespace woc
{
    class Window;

    /// Owns everything that survives a swapchain rebuild. A singleton because exactly one
    /// logical device exists for the process and every renderer subsystem needs it.
    class VulkanDevice final : public Singleton<VulkanDevice>
    {
        friend class Singleton<VulkanDevice>;
    public:
        void Initialise(Window& window, bool enableValidation);
        void Shutdown();

        VkInstance Instance() const { return m_instance; }
        VkSurfaceKHR Surface() const { return m_surface; }
        VkPhysicalDevice PhysicalDevice() const { return m_physicalDevice; }
        VkDevice Handle() const { return m_device; }
        VkQueue GraphicsQueue() const { return m_graphicsQueue; }
        VkQueue PresentQueue() const { return m_presentQueue; }
        u32 GraphicsFamily() const { return m_graphicsFamily; }
        u32 PresentFamily() const { return m_presentFamily; }
        VkCommandPool CommandPool() const { return m_commandPool; }
        const VkPhysicalDeviceProperties& Properties() const { return m_properties; }
        const std::string& DeviceName() const { return m_deviceName; }

        u32 FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const;
        VkFormat FindDepthFormat() const;

        /// Allocates, begins and returns a primary command buffer for immediate work.
        VkCommandBuffer BeginSingleUse() const;
        /// Submits, waits and frees a buffer obtained from BeginSingleUse.
        void EndSingleUse(VkCommandBuffer commandBuffer) const;

        void WaitIdle() const;

    private:
        VulkanDevice() = default;
        ~VulkanDevice() = default;

        void CreateInstance(bool enableValidation);
        void CreateDebugMessenger();
        void CreateSurface(Window& window);
        void PickPhysicalDevice();
        void CreateLogicalDevice(bool enableValidation);

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_presentQueue = VK_NULL_HANDLE;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;

        VkPhysicalDeviceProperties m_properties{};
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};
        std::string m_deviceName;

        u32 m_graphicsFamily = UINT32_MAX;
        u32 m_presentFamily = UINT32_MAX;
        bool m_validationEnabled = false;
    };
}
