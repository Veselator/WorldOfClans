// VulkanBuffer.h - RAII wrappers over device buffers and images.
#pragma once

#include "VulkanCommon.h"

namespace woc
{
    /// A device buffer plus its backing allocation. Host-visible buffers stay mapped for
    /// the lifetime of the object, which suits the per-frame streaming this renderer does.
    class GpuBuffer
    {
    public:
        GpuBuffer() = default;
        ~GpuBuffer();

        GpuBuffer(const GpuBuffer&) = delete;
        GpuBuffer& operator=(const GpuBuffer&) = delete;
        GpuBuffer(GpuBuffer&& other) noexcept { Swap(other); }
        GpuBuffer& operator=(GpuBuffer&& other) noexcept { Swap(other); return *this; }

        void Create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties);
        void Destroy();

        /// Copies `size` bytes into a host-visible buffer at `offset`.
        void Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

        /// Creates a device-local buffer and fills it through a staging copy.
        void CreateDeviceLocal(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

        VkBuffer Handle() const { return m_buffer; }
        VkDeviceSize Size() const { return m_size; }
        void* Mapped() const { return m_mapped; }
        bool IsValid() const { return m_buffer != VK_NULL_HANDLE; }

    private:
        void Swap(GpuBuffer& other) noexcept;

        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        VkDeviceSize m_size = 0;
        void* m_mapped = nullptr;
    };

    /// A sampled 2D image with its view. Used for the sprite atlas, the map layers and
    /// the dynamically regenerated ownership mask.
    class GpuImage
    {
    public:
        GpuImage() = default;
        ~GpuImage();

        GpuImage(const GpuImage&) = delete;
        GpuImage& operator=(const GpuImage&) = delete;
        GpuImage(GpuImage&& other) noexcept { Swap(other); }
        GpuImage& operator=(GpuImage&& other) noexcept { Swap(other); return *this; }

        void Create(u32 width, u32 height, VkFormat format, VkImageUsageFlags usage,
                    VkImageAspectFlags aspect);
        void Destroy();

        /// Uploads tightly packed pixels and leaves the image ready for shader reads.
        /// Synchronous: it allocates a staging buffer and drains the queue, so it belongs
        /// to loading, not to a running frame. Use StagePixels for layers that change.
        void UploadPixels(const void* pixels, VkDeviceSize byteSize);

        /// Copies pixels into a staging buffer this image keeps for the purpose, to be
        /// handed to the GPU inside the next frame's command buffer. Nothing is allocated
        /// after the first call and nothing waits, which is what makes a layer that changes
        /// several times a second affordable at all.
        ///
        /// `frame` is a monotonically rising frame counter and `framesInFlight` how many
        /// frames deep the renderer runs; together they say when the staging buffer is
        /// certainly free again. Returns false when it is not - the caller has simply come
        /// too early and should offer the same data on a later frame.
        bool StagePixels(const void* pixels, VkDeviceSize byteSize, u64 frame, u32 framesInFlight);
        bool HasStagedUpload() const { return m_stagePending; }
        /// Records the staged copy. The renderer calls this inside the frame's command
        /// buffer, before the render pass opens.
        void RecordStagedUpload(VkCommandBuffer cmd, u64 frame);

        void TransitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);

        VkImage Handle() const { return m_image; }
        VkImageView View() const { return m_view; }
        VkFormat Format() const { return m_format; }
        u32 Width() const { return m_width; }
        u32 Height() const { return m_height; }
        bool IsValid() const { return m_image != VK_NULL_HANDLE; }

    private:
        void Swap(GpuImage& other) noexcept;

        VkImage m_image = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        VkImageView m_view = VK_NULL_HANDLE;
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        VkImageAspectFlags m_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        u32 m_width = 0;
        u32 m_height = 0;

        // --- streaming -------------------------------------------------------------------
        GpuBuffer m_staging;            // kept between uploads; never reallocated for the
                                        // same size, which is the whole point
        VkDeviceSize m_stageBytes = 0;
        bool m_stagePending = false;    // staged, not yet recorded
        bool m_stageRecorded = false;   // recorded at least once
        u64 m_stageFrame = 0;           // the frame the last copy was recorded on
    };
}
