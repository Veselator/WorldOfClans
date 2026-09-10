// VulkanSwapchain.h - swapchain, depth buffer, render pass and framebuffers.
#pragma once

#include "VulkanCommon.h"
#include "VulkanBuffer.h"

namespace woc
{
    /// Recreated whenever the window is resized; everything else in the renderer is
    /// written against the render pass this object owns.
    class VulkanSwapchain
    {
    public:
        void Create(u32 width, u32 height, bool vsync);
        void Recreate(u32 width, u32 height);
        void Destroy();

        /// Acquires the next image. Returns false when the swapchain is out of date.
        bool AcquireNextImage(VkSemaphore waitSemaphore, u32& outIndex);
        /// Presents `imageIndex`. Returns false when the swapchain must be rebuilt.
        bool Present(VkSemaphore renderFinished, u32 imageIndex);

        VkSwapchainKHR Handle() const { return m_swapchain; }
        VkRenderPass RenderPass() const { return m_renderPass; }
        VkFramebuffer Framebuffer(u32 index) const { return m_framebuffers[index]; }
        VkExtent2D Extent() const { return m_extent; }
        VkFormat ColorFormat() const { return m_colorFormat; }
        u32 ImageCount() const { return static_cast<u32>(m_images.size()); }

    private:
        void CreateSwapchain(u32 width, u32 height);
        void CreateDepthResources();
        void CreateRenderPass();
        void CreateFramebuffers();
        void DestroyDependents();

        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        std::vector<VkImage> m_images;
        std::vector<VkImageView> m_imageViews;
        std::vector<VkFramebuffer> m_framebuffers;
        GpuImage m_depth;

        VkExtent2D m_extent{};
        VkFormat m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
        VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
        bool m_vsync = true;
    };
}
