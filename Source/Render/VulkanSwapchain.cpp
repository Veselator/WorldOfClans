#include "VulkanSwapchain.h"
#include "VulkanDevice.h"

#include <algorithm>

namespace woc
{
    void VulkanSwapchain::Create(u32 width, u32 height, bool vsync)
    {
        m_vsync = vsync;
        m_depthFormat = VulkanDevice::Get().FindDepthFormat();
        CreateSwapchain(width, height);
        CreateDepthResources();
        CreateRenderPass();
        CreateFramebuffers();
    }

    void VulkanSwapchain::Recreate(u32 width, u32 height)
    {
        VulkanDevice& device = VulkanDevice::Get();
        device.WaitIdle();

        DestroyDependents();
        CreateSwapchain(width, height);
        CreateDepthResources();
        CreateFramebuffers();
    }

    void VulkanSwapchain::CreateSwapchain(u32 width, u32 height)
    {
        VulkanDevice& device = VulkanDevice::Get();
        VkPhysicalDevice gpu = device.PhysicalDevice();
        VkSurfaceKHR surface = device.Surface();

        VkSurfaceCapabilitiesKHR caps{};
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));

        u32 formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, formats.data());

        VkSurfaceFormatKHR chosen = formats.empty()
            ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
            : formats[0];
        for (const VkSurfaceFormatKHR& f : formats)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosen = f;
                break;
            }
        }
        m_colorFormat = chosen.format;

        u32 modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, modes.data());

        m_presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported
        if (!m_vsync)
        {
            for (VkPresentModeKHR mode : modes)
            {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) { m_presentMode = mode; break; }
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) m_presentMode = mode;
            }
        }

        if (caps.currentExtent.width != UINT32_MAX)
        {
            m_extent = caps.currentExtent;
        }
        else
        {
            m_extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
            m_extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        u32 imageCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

        VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = m_extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = m_presentMode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = m_swapchain;

        const u32 families[] = { device.GraphicsFamily(), device.PresentFamily() };
        if (families[0] != families[1])
        {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = families;
        }
        else
        {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSwapchainKHR(device.Handle(), &info, nullptr, &newSwapchain));
        if (m_swapchain) vkDestroySwapchainKHR(device.Handle(), m_swapchain, nullptr);
        m_swapchain = newSwapchain;

        u32 count = 0;
        vkGetSwapchainImagesKHR(device.Handle(), m_swapchain, &count, nullptr);
        m_images.resize(count);
        vkGetSwapchainImagesKHR(device.Handle(), m_swapchain, &count, m_images.data());

        m_imageViews.resize(count);
        for (u32 i = 0; i < count; ++i)
        {
            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image = m_images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_colorFormat;
            viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VK_CHECK(vkCreateImageView(device.Handle(), &viewInfo, nullptr, &m_imageViews[i]));
        }
    }

    void VulkanSwapchain::CreateDepthResources()
    {
        m_depth.Create(m_extent.width, m_extent.height, m_depthFormat,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void VulkanSwapchain::CreateRenderPass()
    {
        VkAttachmentDescription color{};
        color.format = m_colorFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth{};
        depth.format = m_depthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const VkAttachmentDescription attachments[] = { color, depth };

        VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        VK_CHECK(vkCreateRenderPass(VulkanDevice::Get().Handle(), &info, nullptr, &m_renderPass));
    }

    void VulkanSwapchain::CreateFramebuffers()
    {
        VkDevice device = VulkanDevice::Get().Handle();
        m_framebuffers.resize(m_imageViews.size());
        for (size_t i = 0; i < m_imageViews.size(); ++i)
        {
            const VkImageView attachments[] = { m_imageViews[i], m_depth.View() };

            VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            info.renderPass = m_renderPass;
            info.attachmentCount = 2;
            info.pAttachments = attachments;
            info.width = m_extent.width;
            info.height = m_extent.height;
            info.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device, &info, nullptr, &m_framebuffers[i]));
        }
    }

    void VulkanSwapchain::DestroyDependents()
    {
        VkDevice device = VulkanDevice::Get().Handle();
        for (VkFramebuffer fb : m_framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        m_framebuffers.clear();
        for (VkImageView view : m_imageViews) vkDestroyImageView(device, view, nullptr);
        m_imageViews.clear();
        m_depth.Destroy();
    }

    void VulkanSwapchain::Destroy()
    {
        VkDevice device = VulkanDevice::Get().Handle();
        if (!device) return;

        DestroyDependents();
        if (m_renderPass) { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
        if (m_swapchain) { vkDestroySwapchainKHR(device, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
        m_images.clear();
    }

    bool VulkanSwapchain::AcquireNextImage(VkSemaphore waitSemaphore, u32& outIndex)
    {
        const VkResult result = vkAcquireNextImageKHR(
            VulkanDevice::Get().Handle(), m_swapchain, UINT64_MAX, waitSemaphore, VK_NULL_HANDLE, &outIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) VK_CHECK(result);
        return true;
    }

    bool VulkanSwapchain::Present(VkSemaphore renderFinished, u32 imageIndex)
    {
        VkPresentInfoKHR info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &renderFinished;
        info.swapchainCount = 1;
        info.pSwapchains = &m_swapchain;
        info.pImageIndices = &imageIndex;

        const VkResult result = vkQueuePresentKHR(VulkanDevice::Get().PresentQueue(), &info);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return false;
        if (result != VK_SUCCESS) VK_CHECK(result);
        return true;
    }
}
