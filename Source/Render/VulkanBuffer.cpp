#include "VulkanBuffer.h"
#include "VulkanDevice.h"

#include <cstring>
#include <utility>

namespace woc
{
    // ---------------------------------------------------------------------------------
    // GpuBuffer
    // ---------------------------------------------------------------------------------

    GpuBuffer::~GpuBuffer()
    {
        Destroy();
    }

    void GpuBuffer::Swap(GpuBuffer& other) noexcept
    {
        std::swap(m_buffer, other.m_buffer);
        std::swap(m_memory, other.m_memory);
        std::swap(m_size, other.m_size);
        std::swap(m_mapped, other.m_mapped);
    }

    void GpuBuffer::Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties)
    {
        Destroy();
        if (size == 0) return;

        VulkanDevice& device = VulkanDevice::Get();

        VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device.Handle(), &info, nullptr, &m_buffer));

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device.Handle(), m_buffer, &requirements);

        VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = device.FindMemoryType(requirements.memoryTypeBits, memoryProperties);
        VK_CHECK(vkAllocateMemory(device.Handle(), &alloc, nullptr, &m_memory));
        VK_CHECK(vkBindBufferMemory(device.Handle(), m_buffer, m_memory, 0));

        m_size = size;
        if (memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VK_CHECK(vkMapMemory(device.Handle(), m_memory, 0, size, 0, &m_mapped));
        }
    }

    void GpuBuffer::Destroy()
    {
        if (m_buffer == VK_NULL_HANDLE && m_memory == VK_NULL_HANDLE) return;

        VkDevice device = VulkanDevice::Get().Handle();
        if (!device) { m_buffer = VK_NULL_HANDLE; m_memory = VK_NULL_HANDLE; m_mapped = nullptr; m_size = 0; return; }

        if (m_mapped) { vkUnmapMemory(device, m_memory); m_mapped = nullptr; }
        if (m_buffer) { vkDestroyBuffer(device, m_buffer, nullptr); m_buffer = VK_NULL_HANDLE; }
        if (m_memory) { vkFreeMemory(device, m_memory, nullptr); m_memory = VK_NULL_HANDLE; }
        m_size = 0;
    }

    void GpuBuffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset)
    {
        if (!m_mapped || size == 0) return;
        std::memcpy(static_cast<u8*>(m_mapped) + offset, data, static_cast<size_t>(size));
    }

    void GpuBuffer::CreateDeviceLocal(const void* data, VkDeviceSize size, VkBufferUsageFlags usage)
    {
        GpuBuffer staging;
        staging.Create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.Upload(data, size);

        Create(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VulkanDevice& device = VulkanDevice::Get();
        VkCommandBuffer cmd = device.BeginSingleUse();
        VkBufferCopy region{ 0, 0, size };
        vkCmdCopyBuffer(cmd, staging.Handle(), m_buffer, 1, &region);
        device.EndSingleUse(cmd);
    }

    // ---------------------------------------------------------------------------------
    // GpuImage
    // ---------------------------------------------------------------------------------

    GpuImage::~GpuImage()
    {
        Destroy();
    }

    void GpuImage::Swap(GpuImage& other) noexcept
    {
        std::swap(m_image, other.m_image);
        std::swap(m_memory, other.m_memory);
        std::swap(m_view, other.m_view);
        std::swap(m_format, other.m_format);
        std::swap(m_aspect, other.m_aspect);
        std::swap(m_width, other.m_width);
        std::swap(m_height, other.m_height);
    }

    void GpuImage::Create(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage,
                          VkImageAspectFlags aspect)
    {
        Destroy();
        if (width == 0 || height == 0) return;

        VulkanDevice& device = VulkanDevice::Get();
        m_width = width;
        m_height = height;
        m_format = format;
        m_aspect = aspect;

        VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = { width, height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = usage;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateImage(device.Handle(), &info, nullptr, &m_image));

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device.Handle(), m_image, &requirements);

        VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = device.FindMemoryType(requirements.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(device.Handle(), &alloc, nullptr, &m_memory));
        VK_CHECK(vkBindImageMemory(device.Handle(), m_image, m_memory, 0));

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = { aspect, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(device.Handle(), &viewInfo, nullptr, &m_view));
    }

    void GpuImage::Destroy()
    {
        VkDevice device = VulkanDevice::Get().Handle();
        if (!device) { m_image = VK_NULL_HANDLE; m_memory = VK_NULL_HANDLE; m_view = VK_NULL_HANDLE; return; }

        if (m_view) { vkDestroyImageView(device, m_view, nullptr); m_view = VK_NULL_HANDLE; }
        if (m_image) { vkDestroyImage(device, m_image, nullptr); m_image = VK_NULL_HANDLE; }
        if (m_memory) { vkFreeMemory(device, m_memory, nullptr); m_memory = VK_NULL_HANDLE; }
        m_width = m_height = 0;
    }

    void GpuImage::TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image;
        barrier.subresourceRange = { m_aspect, 0, 1, 0, 1 };

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else
        {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void GpuImage::UploadPixels(const void* pixels, VkDeviceSize byteSize)
    {
        if (!m_image || byteSize == 0) return;

        GpuBuffer staging;
        staging.Create(byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.Upload(pixels, byteSize);

        VulkanDevice& device = VulkanDevice::Get();
        VkCommandBuffer cmd = device.BeginSingleUse();

        TransitionLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource = { m_aspect, 0, 0, 1 };
        region.imageExtent = { m_width, m_height, 1 };
        vkCmdCopyBufferToImage(cmd, staging.Handle(), m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        TransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        device.EndSingleUse(cmd);
    }
}
