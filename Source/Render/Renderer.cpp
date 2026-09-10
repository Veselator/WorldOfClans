#include "Renderer.h"
#include "Pipeline.h"
#include "VulkanDevice.h"
#include "../Platform/Window.h"
#include "../Core/Config.h"
#include "../Core/Paths.h"
#include "../Core/ImageIO.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>

namespace woc
{
    namespace
    {
        struct GlobalsUBO
        {
            Mat4 view;
            Mat4 projection;
            Mat4 uiProjection;
            Vec4 params;
        };

        struct TerrainPush
        {
            Vec4 forestRect;
            Vec4 fieldRect;
            Vec4 settings;
            Vec4 texel;
            Vec4 water;
            Vec4 flags;
        };

        struct WaterPush
        {
            Vec4 deepColor;
            Vec4 shallowColor;
            Vec4 settings;
        };

        struct UIPush
        {
            Vec4 mode;
        };

        constexpr u32 kMaxOwnerSlots = 32;

        f64 NowSeconds()
        {
            using namespace std::chrono;
            return duration<f64>(steady_clock::now().time_since_epoch()).count();
        }
    }

    // =====================================================================================
    // Set-up
    // =====================================================================================

    void Renderer::Initialise(Window& window)
    {
        m_window = &window;
        ConfigManager& config = ConfigManager::Get();

        VulkanDevice::Get().Initialise(window, config.Bool("render/validation", false));
        m_swapchain.Create(window.Width(), window.Height(), config.Bool("render/vsync", true));

        CreateSamplers();
        CreateDescriptorInfrastructure();
        CreateDefaultTextures();
        CreatePipelines();
        CreateFrameResources();

        m_camera.Configure(
            config.Float("camera/pitchDegrees", 55.0f),
            config.Float("camera/zoom/min", 0.35f),
            config.Float("camera/zoom/max", 6.0f),
            config.Float("camera/zoom/default", 1.0f));
        m_camera.SetViewport(static_cast<f32>(window.Width()), static_cast<f32>(window.Height()));

        m_forestTiling = config.Float("render/forestTiling", 48.0f);
        m_fieldTiling = config.Float("render/fieldTiling", 72.0f);
        m_ownerTint = config.Float("render/ownerTint", 0.3f);
        m_borderWidth = config.Float("render/borderWidth", 1.5f);

        m_lastFrameTime = NowSeconds();
        WOC_LOG_INFO("Renderer ready");
    }

    void Renderer::CreateSamplers()
    {
        VkDevice device = VulkanDevice::Get().Handle();

        VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        VK_CHECK(vkCreateSampler(device, &info, nullptr, &m_nearestSampler));

        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        VK_CHECK(vkCreateSampler(device, &info, nullptr, &m_linearSampler));
    }

    void Renderer::CreateDescriptorInfrastructure()
    {
        VkDevice device = VulkanDevice::Get().Handle();

        const VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 32;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = sizes;
        VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

        // set 0: per-frame globals
        VkDescriptorSetLayoutBinding globalBinding{};
        globalBinding.binding = 0;
        globalBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        globalBinding.descriptorCount = 1;
        globalBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &globalBinding;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_globalLayout));

        // set 1 (sprites / UI): one combined image sampler
        VkDescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 0;
        textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureBinding.descriptorCount = 1;
        textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &textureBinding;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_textureLayout));

        // set 1 (terrain): five map layers plus the realm palette
        VkDescriptorSetLayoutBinding terrainBindings[6]{};
        for (u32 i = 0; i < 5; ++i)
        {
            terrainBindings[i].binding = i;
            terrainBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            terrainBindings[i].descriptorCount = 1;
            terrainBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        terrainBindings[5].binding = 5;
        terrainBindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        terrainBindings[5].descriptorCount = 1;
        terrainBindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        layoutInfo.bindingCount = 6;
        layoutInfo.pBindings = terrainBindings;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_terrainLayout));
    }

    VkDescriptorSet Renderer::CreateTextureSet(VkImageView view, VkSampler sampler)
    {
        VkDevice device = VulkanDevice::Get().Handle();

        VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc.descriptorPool = m_descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_textureLayout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateDescriptorSets(device, &alloc, &set));

        VkDescriptorImageInfo imageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return set;
    }

    void Renderer::CreateDefaultTextures()
    {
        ConfigManager& config = ConfigManager::Get();

        // --- sprite sheet -------------------------------------------------------------------
        ImageData atlas;
        const std::string atlasPath = Paths::Get().Sprite(config.Str("sprites/file", "WoCVisual.png"));
        if (!LoadImage(atlasPath, atlas, 4))
        {
            throw std::runtime_error("Sprite sheet missing: " + atlasPath);
        }
        m_atlasImage.Create(atlas.width, atlas.height, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
        m_atlasImage.UploadPixels(atlas.pixels.data(), atlas.pixels.size());
        m_atlasSet = CreateTextureSet(m_atlasImage.View(), m_nearestSampler);

        // Tile rectangles are derived from the sheet layout described in the config.
        const u32 tileSize = static_cast<u32>(config.Int("sprites/tileSize", 16));
        const u32 columns = std::max(1u, atlas.width / std::max(1u, tileSize));
        m_atlasUVs.resize(static_cast<size_t>(SpriteId::Count));
        for (u32 i = 0; i < static_cast<u32>(SpriteId::Count); ++i)
        {
            const u32 column = i % columns;
            const u32 row = i / columns;
            // A half-texel inset stops neighbouring tiles bleeding in when magnified.
            const f32 inset = 0.0f;
            m_atlasUVs[i] = {
                (column * tileSize + inset) / static_cast<f32>(atlas.width),
                (row * tileSize + inset) / static_cast<f32>(atlas.height),
                ((column + 1) * tileSize - inset) / static_cast<f32>(atlas.width),
                ((row + 1) * tileSize - inset) / static_cast<f32>(atlas.height)
            };
        }

        // --- font ----------------------------------------------------------------------------
        const std::string fontName = config.Str("ui/font", "Consolas");
        const i32 fontSize = config.Int("ui/fontPixelHeight", 15);
        if (!m_font.Build(fontName, fontSize, false))
        {
            WOC_LOG_WARN("Could not rasterise '", fontName, "'; falling back to Courier New");
            m_font.Build("Courier New", fontSize, false);
        }
        m_fontImage.Create(m_font.Width(), m_font.Height(), VK_FORMAT_R8_UNORM,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT);
        m_fontImage.UploadPixels(m_font.Pixels().data(), m_font.Pixels().size());
        m_fontSet = CreateTextureSet(m_fontImage.View(), m_linearSampler);

        // --- 1x1 white --------------------------------------------------------------------------
        const u8 white[4] = { 255, 255, 255, 255 };
        m_whiteImage.Create(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
        m_whiteImage.UploadPixels(white, sizeof(white));
        m_whiteSet = CreateTextureSet(m_whiteImage.View(), m_nearestSampler);

        // --- placeholder map layers -------------------------------------------------------------
        const u8 zero = 0;
        m_terrainColor.Create(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        m_terrainColor.UploadPixels(white, sizeof(white));
        for (GpuImage* mask : { &m_treeMask, &m_fieldMask, &m_ownerMask })
        {
            mask->Create(1, 1, VK_FORMAT_R8_UNORM,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT);
            mask->UploadPixels(&zero, 1);
        }

        m_palette.Create(sizeof(Vec4) * kMaxOwnerSlots,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::vector<Vec4> defaultPalette(kMaxOwnerSlots, Vec4{ 0.5f, 0.5f, 0.5f, 0.0f });
        m_palette.Upload(defaultPalette.data(), sizeof(Vec4) * kMaxOwnerSlots);

        VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc.descriptorPool = m_descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_terrainLayout;
        VK_CHECK(vkAllocateDescriptorSets(VulkanDevice::Get().Handle(), &alloc, &m_terrainSet));
        RefreshTerrainDescriptor();
    }

    void Renderer::RefreshTerrainDescriptor()
    {
        VkDevice device = VulkanDevice::Get().Handle();

        const VkDescriptorImageInfo images[5] = {
            { m_nearestSampler, m_terrainColor.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_linearSampler,  m_treeMask.View(),     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_linearSampler,  m_fieldMask.View(),    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_nearestSampler, m_ownerMask.View(),    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { m_nearestSampler, m_atlasImage.View(),   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        };

        VkWriteDescriptorSet writes[6]{};
        for (u32 i = 0; i < 5; ++i)
        {
            writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[i].dstSet = m_terrainSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }

        VkDescriptorBufferInfo paletteInfo{ m_palette.Handle(), 0, sizeof(Vec4) * kMaxOwnerSlots };
        writes[5] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[5].dstSet = m_terrainSet;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[5].pBufferInfo = &paletteInfo;

        vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);
    }

    void Renderer::CreatePipelines()
    {
        VkDevice device = VulkanDevice::Get().Handle();
        Paths& paths = Paths::Get();

        // --- layouts --------------------------------------------------------------------------
        {
            const VkDescriptorSetLayout sets[] = { m_globalLayout, m_terrainLayout };
            VkPushConstantRange range{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TerrainPush) };
            VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            info.setLayoutCount = 2;
            info.pSetLayouts = sets;
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = &range;
            VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &m_terrainPipelineLayout));
        }
        {
            const VkDescriptorSetLayout sets[] = { m_globalLayout, m_textureLayout };
            VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            info.setLayoutCount = 2;
            info.pSetLayouts = sets;
            VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &m_spritePipelineLayout));

            VkPushConstantRange range{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIPush) };
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = &range;
            VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &m_uiPipelineLayout));
        }

        {
            const VkDescriptorSetLayout sets[] = { m_globalLayout };
            VkPushConstantRange range{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterPush) };
            VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            info.setLayoutCount = 1;
            info.pSetLayouts = sets;
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = &range;
            VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &m_waterPipelineLayout));
        }

        // --- sea plane ---------------------------------------------------------------------------
        {
            VkShaderModule vertex = LoadShaderModule(paths.Shader("water.vert.spv"));
            VkShaderModule fragment = LoadShaderModule(paths.Shader("water.frag.spv"));
            m_waterPipeline = PipelineBuilder()
                .Shaders(vertex, fragment)
                .VertexBinding(0, sizeof(Vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                .VertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .DepthTest(false, false)
                .AlphaBlend(false)
                .Layout(m_waterPipelineLayout)
                .Build(m_swapchain.RenderPass());
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
        }

        // --- terrain ---------------------------------------------------------------------------
        {
            VkShaderModule vertex = LoadShaderModule(paths.Shader("terrain.vert.spv"));
            VkShaderModule fragment = LoadShaderModule(paths.Shader("terrain.frag.spv"));
            m_terrainPipeline = PipelineBuilder()
                .Shaders(vertex, fragment)
                .VertexBinding(0, sizeof(TerrainVertex), VK_VERTEX_INPUT_RATE_VERTEX)
                .VertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TerrainVertex, position))
                .VertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TerrainVertex, uv))
                .VertexAttribute(2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TerrainVertex, normal))
                .DepthTest(true, true)
                .AlphaBlend(false)
                .Layout(m_terrainPipelineLayout)
                .Build(m_swapchain.RenderPass());
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
        }

        // --- sprites ----------------------------------------------------------------------------
        {
            VkShaderModule vertex = LoadShaderModule(paths.Shader("sprite.vert.spv"));
            VkShaderModule fragment = LoadShaderModule(paths.Shader("sprite.frag.spv"));
            m_spritePipeline = PipelineBuilder()
                .Shaders(vertex, fragment)
                .VertexBinding(0, sizeof(SpriteInstance), VK_VERTEX_INPUT_RATE_INSTANCE)
                .VertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SpriteInstance, worldPosition))
                .VertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteInstance, size))
                .VertexAttribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteInstance, uvRect))
                .VertexAttribute(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteInstance, color))
                .VertexAttribute(4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteInstance, params))
                .DepthTest(false, false)
                .AlphaBlend(true)
                .Layout(m_spritePipelineLayout)
                .Build(m_swapchain.RenderPass());
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
        }

        // --- interface ----------------------------------------------------------------------------
        {
            VkShaderModule vertex = LoadShaderModule(paths.Shader("ui.vert.spv"));
            VkShaderModule fragment = LoadShaderModule(paths.Shader("ui.frag.spv"));
            m_uiPipeline = PipelineBuilder()
                .Shaders(vertex, fragment)
                .VertexBinding(0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX)
                .VertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, position))
                .VertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, uv))
                .VertexAttribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, color))
                .DepthTest(false, false)
                .AlphaBlend(true)
                .Layout(m_uiPipelineLayout)
                .Build(m_swapchain.RenderPass());
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, fragment, nullptr);
        }
    }

    void Renderer::CreateFrameResources()
    {
        VkDevice device = VulkanDevice::Get().Handle();

        VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = VulkanDevice::Get().CommandPool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (FrameData& frame : m_frames)
        {
            VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frame.command));
            VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable));
            VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight));

            frame.globals.Create(sizeof(GlobalsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            VkDescriptorSetAllocateInfo setAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            setAlloc.descriptorPool = m_descriptorPool;
            setAlloc.descriptorSetCount = 1;
            setAlloc.pSetLayouts = &m_globalLayout;
            VK_CHECK(vkAllocateDescriptorSets(device, &setAlloc, &frame.globalSet));

            VkDescriptorBufferInfo bufferInfo{ frame.globals.Handle(), 0, sizeof(GlobalsUBO) };
            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = frame.globalSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

            EnsureFrameCapacity(frame, 2048, 8192);
        }

        m_renderFinished.resize(m_swapchain.ImageCount());
        for (VkSemaphore& semaphore : m_renderFinished)
        {
            VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
        }
    }

    void Renderer::EnsureFrameCapacity(FrameData& frame, u32 spriteCount, u32 uiVertexCount)
    {
        if (spriteCount > frame.spriteCapacity)
        {
            const u32 capacity = std::max(spriteCount * 2u, 1024u);
            frame.spriteInstances.Create(sizeof(SpriteInstance) * capacity,
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            frame.spriteCapacity = capacity;
        }
        if (uiVertexCount > frame.uiCapacity)
        {
            const u32 capacity = std::max(uiVertexCount * 2u, 4096u);
            frame.uiVertices.Create(sizeof(UIVertex) * capacity,
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            frame.uiCapacity = capacity;
        }
    }

    // =====================================================================================
    // Map layers
    // =====================================================================================

    void Renderer::SetTerrainMesh(const TerrainMesh& mesh)
    {
        VulkanDevice::Get().WaitIdle();
        m_terrainVertices.Destroy();
        m_terrainIndices.Destroy();
        m_terrainIndexCount = 0;
        if (mesh.vertices.empty() || mesh.indices.empty()) return;

        m_terrainVertices.CreateDeviceLocal(mesh.vertices.data(),
                                            sizeof(TerrainVertex) * mesh.vertices.size(),
                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_terrainIndices.CreateDeviceLocal(mesh.indices.data(),
                                           sizeof(u32) * mesh.indices.size(),
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        m_terrainIndexCount = static_cast<u32>(mesh.indices.size());
        WOC_LOG_INFO("Terrain mesh uploaded: ", mesh.vertices.size(), " vertices, ",
                     m_terrainIndexCount / 3, " triangles");
    }

    void Renderer::UpdateMask(GpuImage& image, const std::vector<u8>& mask, u32 width, u32 height)
    {
        if (mask.empty() || width == 0 || height == 0) return;

        VulkanDevice::Get().WaitIdle();
        if (image.Width() != width || image.Height() != height)
        {
            image.Create(width, height, VK_FORMAT_R8_UNORM,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT);
            image.UploadPixels(mask.data(), mask.size());
            RefreshTerrainDescriptor();
        }
        else
        {
            image.UploadPixels(mask.data(), mask.size());
        }
    }

    void Renderer::SetWaterPlane(const Vec2& mapSize, f32 margin)
    {
        VulkanDevice::Get().WaitIdle();

        // A single quad, far larger than the map, lying on the water datum. The camera is
        // clamped to the map, so this always fills the view whatever the zoom.
        const f32 minX = -margin;
        const f32 maxX = mapSize.x + margin;
        const f32 minY = -margin;
        const f32 maxY = mapSize.y + margin;

        const Vec3 corners[6] = {
            Camera::ToWorld({ minX, minY }, 0.0f),
            Camera::ToWorld({ maxX, minY }, 0.0f),
            Camera::ToWorld({ maxX, maxY }, 0.0f),
            Camera::ToWorld({ minX, minY }, 0.0f),
            Camera::ToWorld({ maxX, maxY }, 0.0f),
            Camera::ToWorld({ minX, maxY }, 0.0f),
        };

        m_waterVertices.CreateDeviceLocal(corners, sizeof(corners), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_waterVertexCount = 6;
    }

    void Renderer::RecordWater(VkCommandBuffer cmd, FrameData& frame)
    {
        if (!m_terrainEnabled || m_waterVertexCount == 0) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_waterPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_waterPipelineLayout,
                                0, 1, &frame.globalSet, 0, nullptr);

        ConfigManager& config = ConfigManager::Get();
        WaterPush push{};
        const Color deep = Color::FromRGB(static_cast<u32>(
            std::strtoul(config.Str("render/water/deepColor", "007f95").c_str(), nullptr, 16)));
        const Color shallow = Color::FromRGB(static_cast<u32>(
            std::strtoul(config.Str("render/water/shallowColor", "00e6dd").c_str(), nullptr, 16)));

        push.deepColor = { deep.r, deep.g, deep.b, 1.0f };
        push.shallowColor = { shallow.r, shallow.g, shallow.b, 1.0f };
        push.settings = { config.Float("render/water/waveScale", 0.006f),
                          config.Float("render/water/waveSpeed", 0.55f),
                          config.Float("render/water/glint", 0.14f),
                          config.Float("render/water/chop", 0.28f) };
        vkCmdPushConstants(cmd, m_waterPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        const VkDeviceSize offset = 0;
        VkBuffer buffer = m_waterVertices.Handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
        vkCmdDraw(cmd, m_waterVertexCount, 1, 0, 0);
    }

    void Renderer::SetTerrainColor(const std::vector<u8>& colorRGBA, u32 width, u32 height)
    {
        if (colorRGBA.empty() || width == 0 || height == 0) return;

        VulkanDevice::Get().WaitIdle();
        m_terrainColor.Create(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
        m_terrainColor.UploadPixels(colorRGBA.data(), colorRGBA.size());
        RefreshTerrainDescriptor();
        m_terrainTexturesReady = true;
    }

    void Renderer::SetTreeMask(const std::vector<u8>& mask, u32 width, u32 height)
    {
        UpdateMask(m_treeMask, mask, width, height);
    }

    void Renderer::SetFieldMask(const std::vector<u8>& mask, u32 width, u32 height)
    {
        UpdateMask(m_fieldMask, mask, width, height);
    }

    void Renderer::SetOwnerMask(const std::vector<u8>& mask, u32 width, u32 height)
    {
        UpdateMask(m_ownerMask, mask, width, height);
    }

    void Renderer::SetOwnerPalette(const std::vector<Color>& colors)
    {
        std::vector<Vec4> data(kMaxOwnerSlots, Vec4{ 0.5f, 0.5f, 0.5f, 0.0f });
        for (size_t i = 0; i < colors.size() && i < kMaxOwnerSlots; ++i)
        {
            data[i] = { colors[i].r, colors[i].g, colors[i].b, colors[i].a };
        }
        m_palette.Upload(data.data(), sizeof(Vec4) * kMaxOwnerSlots);
    }

    // =====================================================================================
    // Submission
    // =====================================================================================

    Vec4 Renderer::SpriteUV(SpriteId sprite) const
    {
        const size_t index = static_cast<size_t>(sprite);
        return index < m_atlasUVs.size() ? m_atlasUVs[index] : Vec4{ 0, 0, 1, 1 };
    }

    void Renderer::DrawSprite(SpriteId sprite, const Vec2& mapPosition, f32 height, f32 worldSize,
                              const Color& tint, f32 anchor, f32 flash)
    {
        SpriteInstance instance;
        instance.worldPosition = Camera::ToWorld(mapPosition, height);
        instance.size = { worldSize, worldSize };
        instance.uvRect = SpriteUV(sprite);
        instance.color = tint;
        instance.params = { anchor, 0.0f, flash, 0.0f };
        m_sprites.push_back(instance);
    }

    void Renderer::DrawSpriteRaw(const SpriteInstance& instance)
    {
        m_sprites.push_back(instance);
    }

    VkRect2D Renderer::ActiveScissor() const
    {
        const VkExtent2D extent = m_swapchain.Extent();
        if (m_clipStack.empty()) return VkRect2D{ { 0, 0 }, extent };

        const Rect& r = m_clipStack.back();
        const i32 x = std::clamp(static_cast<i32>(r.x), 0, static_cast<i32>(extent.width));
        const i32 y = std::clamp(static_cast<i32>(r.y), 0, static_cast<i32>(extent.height));
        const i32 right = std::clamp(static_cast<i32>(r.Right()), x, static_cast<i32>(extent.width));
        const i32 bottom = std::clamp(static_cast<i32>(r.Bottom()), y, static_cast<i32>(extent.height));
        return VkRect2D{ { x, y }, { static_cast<u32>(right - x), static_cast<u32>(bottom - y) } };
    }

    Renderer::UIDrawRange& Renderer::CurrentRange(UIDrawMode mode)
    {
        const VkRect2D scissor = ActiveScissor();
        if (!m_uiRanges.empty())
        {
            UIDrawRange& last = m_uiRanges.back();
            const bool sameScissor = last.scissor.offset.x == scissor.offset.x &&
                                     last.scissor.offset.y == scissor.offset.y &&
                                     last.scissor.extent.width == scissor.extent.width &&
                                     last.scissor.extent.height == scissor.extent.height;
            if (last.mode == mode && sameScissor) return last;
        }
        m_uiRanges.push_back({ mode, static_cast<u32>(m_uiVertices.size()), 0, scissor });
        return m_uiRanges.back();
    }

    void Renderer::PushUIQuad(const Rect& rect, const Vec4& uv, const Color& color, UIDrawMode mode)
    {
        UIDrawRange& range = CurrentRange(mode);
        const Vec2 topLeft{ rect.x, rect.y };
        const Vec2 topRight{ rect.Right(), rect.y };
        const Vec2 bottomRight{ rect.Right(), rect.Bottom() };
        const Vec2 bottomLeft{ rect.x, rect.Bottom() };

        m_uiVertices.push_back({ topLeft,     { uv.x, uv.y }, color });
        m_uiVertices.push_back({ topRight,    { uv.z, uv.y }, color });
        m_uiVertices.push_back({ bottomRight, { uv.z, uv.w }, color });
        m_uiVertices.push_back({ topLeft,     { uv.x, uv.y }, color });
        m_uiVertices.push_back({ bottomRight, { uv.z, uv.w }, color });
        m_uiVertices.push_back({ bottomLeft,  { uv.x, uv.w }, color });
        range.count += 6;
    }

    void Renderer::PushUITriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color, UIDrawMode mode)
    {
        UIDrawRange& range = CurrentRange(mode);
        m_uiVertices.push_back({ a, { 0.0f, 0.0f }, color });
        m_uiVertices.push_back({ b, { 1.0f, 0.0f }, color });
        m_uiVertices.push_back({ c, { 1.0f, 1.0f }, color });
        range.count += 3;
    }

    void Renderer::UIRect(const Rect& rect, const Color& color)
    {
        if (rect.w <= 0.0f || rect.h <= 0.0f || color.a <= 0.0f) return;
        PushUIQuad(rect, { 0, 0, 1, 1 }, color, UIDrawMode::Solid);
    }

    void Renderer::UIRectOutline(const Rect& rect, const Color& color, f32 thickness)
    {
        if (rect.w <= 0.0f || rect.h <= 0.0f) return;
        UIRect({ rect.x, rect.y, rect.w, thickness }, color);
        UIRect({ rect.x, rect.Bottom() - thickness, rect.w, thickness }, color);
        UIRect({ rect.x, rect.y + thickness, thickness, rect.h - 2 * thickness }, color);
        UIRect({ rect.Right() - thickness, rect.y + thickness, thickness, rect.h - 2 * thickness }, color);
    }

    void Renderer::UILine(const Vec2& from, const Vec2& to, const Color& color, f32 thickness)
    {
        const Vec2 delta = to - from;
        const f32 length = delta.Length();
        if (length < 0.01f) return;

        const Vec2 direction = delta / length;
        const Vec2 normal{ -direction.y * thickness * 0.5f, direction.x * thickness * 0.5f };

        UIDrawRange& range = CurrentRange(UIDrawMode::Solid);
        const Vec2 a = from + normal;
        const Vec2 b = to + normal;
        const Vec2 c = to - normal;
        const Vec2 d = from - normal;
        m_uiVertices.push_back({ a, { 0, 0 }, color });
        m_uiVertices.push_back({ b, { 1, 0 }, color });
        m_uiVertices.push_back({ c, { 1, 1 }, color });
        m_uiVertices.push_back({ a, { 0, 0 }, color });
        m_uiVertices.push_back({ c, { 1, 1 }, color });
        m_uiVertices.push_back({ d, { 0, 1 }, color });
        range.count += 6;
    }

    void Renderer::UISprite(SpriteId sprite, const Rect& rect, const Color& tint)
    {
        PushUIQuad(rect, SpriteUV(sprite), tint, UIDrawMode::Sprite);
    }

    void Renderer::UIText(const std::string& text, const Vec2& position, const Color& color, f32 scale)
    {
        if (text.empty() || color.a <= 0.0f) return;

        f32 penX = position.x;
        f32 penY = position.y + m_font.Ascent() * scale;
        size_t index = 0;
        while (index < text.size())
        {
            const u32 codepoint = FontAtlas::DecodeUtf8(text, index);
            if (codepoint == '\n')
            {
                penX = position.x;
                penY += m_font.LineHeight() * scale;
                continue;
            }
            const Glyph* glyph = m_font.Find(codepoint);
            if (!glyph) continue;

            if (glyph->width > 0.0f && glyph->height > 0.0f)
            {
                const Rect quad{
                    penX + glyph->bearingX * scale,
                    penY - glyph->bearingY * scale,
                    glyph->width * scale,
                    glyph->height * scale
                };
                PushUIQuad(quad, { glyph->uvMin.x, glyph->uvMin.y, glyph->uvMax.x, glyph->uvMax.y },
                           color, UIDrawMode::Glyph);
            }
            penX += glyph->advance * scale;
        }
    }

    void Renderer::UITextCentered(const std::string& text, const Rect& rect, const Color& color, f32 scale)
    {
        const f32 width = TextWidth(text, scale);
        const f32 height = m_font.LineHeight() * scale;
        UIText(text, { rect.x + (rect.w - width) * 0.5f, rect.y + (rect.h - height) * 0.5f }, color, scale);
    }

    f32 Renderer::TextWidth(const std::string& text, f32 scale) const
    {
        return m_font.MeasureWidth(text) * scale;
    }

    void Renderer::PushClip(const Rect& rect)
    {
        if (m_clipStack.empty())
        {
            m_clipStack.push_back(rect);
            return;
        }
        // Nested clips intersect rather than replace.
        const Rect& parent = m_clipStack.back();
        const f32 x = std::max(parent.x, rect.x);
        const f32 y = std::max(parent.y, rect.y);
        const f32 right = std::min(parent.Right(), rect.Right());
        const f32 bottom = std::min(parent.Bottom(), rect.Bottom());
        m_clipStack.push_back({ x, y, std::max(0.0f, right - x), std::max(0.0f, bottom - y) });
    }

    void Renderer::PopClip()
    {
        if (!m_clipStack.empty()) m_clipStack.pop_back();
    }

    // =====================================================================================
    // Frame loop
    // =====================================================================================

    Vec2 Renderer::ViewportSize() const
    {
        const VkExtent2D extent = m_swapchain.Extent();
        return { static_cast<f32>(extent.width), static_cast<f32>(extent.height) };
    }

    void Renderer::OnResize(u32 width, u32 height)
    {
        if (width == 0 || height == 0) return;
        m_swapchain.Recreate(width, height);
        m_camera.SetViewport(static_cast<f32>(m_swapchain.Extent().width),
                             static_cast<f32>(m_swapchain.Extent().height));
        m_needsResize = false;
    }

    bool Renderer::BeginFrame()
    {
        if (!m_window || m_window->IsMinimised()) return false;

        const f64 now = NowSeconds();
        m_deltaTime = static_cast<f32>(std::min(now - m_lastFrameTime, 0.25));
        m_lastFrameTime = now;
        m_elapsed += m_deltaTime;
        m_fps = m_deltaTime > 0.0f ? Lerp(m_fps, 1.0f / m_deltaTime, 0.1f) : m_fps;

        if (m_needsResize)
        {
            OnResize(m_window->Width(), m_window->Height());
        }

        VkDevice device = VulkanDevice::Get().Handle();
        FrameData& frame = m_frames[m_frameIndex];

        VK_CHECK(vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

        if (!m_swapchain.AcquireNextImage(frame.imageAvailable, m_imageIndex))
        {
            OnResize(m_window->Width(), m_window->Height());
            return false;
        }

        VK_CHECK(vkResetFences(device, 1, &frame.inFlight));

        m_sprites.clear();
        m_uiVertices.clear();
        m_uiRanges.clear();
        m_clipStack.clear();
        m_frameActive = true;
        return true;
    }

    void Renderer::RecordTerrain(VkCommandBuffer cmd, FrameData& frame)
    {
        if (!m_terrainEnabled || m_terrainIndexCount == 0 || !m_terrainTexturesReady) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_terrainPipeline);
        const VkDescriptorSet sets[] = { frame.globalSet, m_terrainSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_terrainPipelineLayout,
                                0, 2, sets, 0, nullptr);

        TerrainPush push{};
        push.forestRect = SpriteUV(SpriteId::Forest);
        push.fieldRect = SpriteUV(SpriteId::Field);
        push.settings = { m_forestTiling, m_fieldTiling, m_borderWidth, m_ownerTint };
        push.texel = {
            m_ownerMask.Width() > 0 ? 1.0f / m_ownerMask.Width() : 0.0f,
            m_ownerMask.Height() > 0 ? 1.0f / m_ownerMask.Height() : 0.0f,
            -0.35f, -0.55f
        };
        // The terrain pass discards anything painted in the water colour, letting the
        // animated sea plane show through for both the ocean and the rivers.
        const Color water = Color::FromRGB(static_cast<u32>(
            std::strtoul(ConfigManager::Get().Str("render/water/mapColor", "00fff6").c_str(), nullptr, 16)));
        push.water = { water.r, water.g, water.b,
                       ConfigManager::Get().Float("render/water/matchTolerance", 0.12f) };
        push.flags = { m_bordersVisible ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };

        vkCmdPushConstants(cmd, m_terrainPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        const VkDeviceSize offset = 0;
        VkBuffer vertexBuffer = m_terrainVertices.Handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, m_terrainIndices.Handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_terrainIndexCount, 1, 0, 0, 0);
    }

    void Renderer::RecordSprites(VkCommandBuffer cmd, FrameData& frame)
    {
        if (m_sprites.empty()) return;

        // Painter's order: sprites do not write depth, so the far ones go down first.
        // Depth is measured along the view axis so the order survives camera rotation.
        const Mat4 view = m_camera.View();
        auto viewDepth = [&view](const Vec3& p)
        {
            return view.m[2] * p.x + view.m[6] * p.y + view.m[10] * p.z + view.m[14];
        };
        std::sort(m_sprites.begin(), m_sprites.end(),
            [&](const SpriteInstance& a, const SpriteInstance& b)
            {
                const f32 da = viewDepth(a.worldPosition) - a.params.y;
                const f32 db = viewDepth(b.worldPosition) - b.params.y;
                return da < db;
            });

        EnsureFrameCapacity(frame, static_cast<u32>(m_sprites.size()), 0);
        frame.spriteInstances.Upload(m_sprites.data(), sizeof(SpriteInstance) * m_sprites.size());

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_spritePipeline);
        const VkDescriptorSet sets[] = { frame.globalSet, m_atlasSet };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_spritePipelineLayout,
                                0, 2, sets, 0, nullptr);

        const VkDeviceSize offset = 0;
        VkBuffer buffer = frame.spriteInstances.Handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
        vkCmdDraw(cmd, 6, static_cast<u32>(m_sprites.size()), 0, 0);
    }

    void Renderer::RecordUI(VkCommandBuffer cmd, FrameData& frame)
    {
        if (m_uiVertices.empty()) return;

        EnsureFrameCapacity(frame, 0, static_cast<u32>(m_uiVertices.size()));
        frame.uiVertices.Upload(m_uiVertices.data(), sizeof(UIVertex) * m_uiVertices.size());

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipeline);
        const VkDeviceSize offset = 0;
        VkBuffer buffer = frame.uiVertices.Handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);

        VkDescriptorSet lastTexture = VK_NULL_HANDLE;
        for (const UIDrawRange& range : m_uiRanges)
        {
            if (range.count == 0) continue;

            VkDescriptorSet texture = m_whiteSet;
            if (range.mode == UIDrawMode::Glyph) texture = m_fontSet;
            else if (range.mode == UIDrawMode::Sprite) texture = m_atlasSet;

            if (texture != lastTexture)
            {
                const VkDescriptorSet sets[] = { frame.globalSet, texture };
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipelineLayout,
                                        0, 2, sets, 0, nullptr);
                lastTexture = texture;
            }

            UIPush push{};
            push.mode = { static_cast<f32>(range.mode), 0.0f, 0.0f, 0.0f };
            vkCmdPushConstants(cmd, m_uiPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

            vkCmdSetScissor(cmd, 0, 1, &range.scissor);
            vkCmdDraw(cmd, range.count, 1, range.first, 0);
        }
    }

    void Renderer::EndFrame()
    {
        if (!m_frameActive) return;
        m_frameActive = false;

        FrameData& frame = m_frames[m_frameIndex];
        const VkExtent2D extent = m_swapchain.Extent();

        GlobalsUBO globals{};
        globals.view = m_camera.View();
        globals.projection = m_camera.Projection();
        globals.uiProjection = Mat4::Ortho(0.0f, static_cast<f32>(extent.width),
                                           0.0f, static_cast<f32>(extent.height), 0.0f, 1.0f);
        globals.params = { m_elapsed, static_cast<f32>(extent.width), static_cast<f32>(extent.height),
                           m_camera.Zoom() };
        frame.globals.Upload(&globals, sizeof(globals));

        VkCommandBuffer cmd = frame.command;
        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        VkClearValue clears[2]{};
        clears[0].color = { { m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a } };
        clears[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        renderPassInfo.renderPass = m_swapchain.RenderPass();
        renderPassInfo.framebuffer = m_swapchain.Framebuffer(m_imageIndex);
        renderPassInfo.renderArea = { { 0, 0 }, extent };
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{ 0.0f, 0.0f, static_cast<f32>(extent.width), static_cast<f32>(extent.height), 0.0f, 1.0f };
        VkRect2D fullScissor{ { 0, 0 }, extent };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &fullScissor);

        RecordWater(cmd, frame);
        RecordTerrain(cmd, frame);
        RecordSprites(cmd, frame);
        vkCmdSetScissor(cmd, 0, 1, &fullScissor);
        RecordUI(cmd, frame);

        vkCmdEndRenderPass(cmd);
        VK_CHECK(vkEndCommandBuffer(cmd));

        VkSemaphore signal = m_renderFinished[m_imageIndex];
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &frame.imageAvailable;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signal;
        VK_CHECK(vkQueueSubmit(VulkanDevice::Get().GraphicsQueue(), 1, &submit, frame.inFlight));

        if (!m_swapchain.Present(signal, m_imageIndex))
        {
            m_needsResize = true;
        }

        m_frameIndex = (m_frameIndex + 1) % kFramesInFlight;
    }

    void Renderer::DestroyPipelines()
    {
        VkDevice device = VulkanDevice::Get().Handle();
        if (!device) return;

        for (VkPipeline* pipeline : { &m_waterPipeline, &m_terrainPipeline, &m_spritePipeline, &m_uiPipeline })
        {
            if (*pipeline) { vkDestroyPipeline(device, *pipeline, nullptr); *pipeline = VK_NULL_HANDLE; }
        }
        for (VkPipelineLayout* layout : { &m_waterPipelineLayout, &m_terrainPipelineLayout,
                                          &m_spritePipelineLayout, &m_uiPipelineLayout })
        {
            if (*layout) { vkDestroyPipelineLayout(device, *layout, nullptr); *layout = VK_NULL_HANDLE; }
        }
    }

    void Renderer::Shutdown()
    {
        VulkanDevice& vulkan = VulkanDevice::Get();
        VkDevice device = vulkan.Handle();
        if (!device) return;
        vulkan.WaitIdle();

        for (FrameData& frame : m_frames)
        {
            frame.globals.Destroy();
            frame.spriteInstances.Destroy();
            frame.uiVertices.Destroy();
            if (frame.imageAvailable) vkDestroySemaphore(device, frame.imageAvailable, nullptr);
            if (frame.inFlight) vkDestroyFence(device, frame.inFlight, nullptr);
            frame = FrameData{};
        }
        for (VkSemaphore semaphore : m_renderFinished) vkDestroySemaphore(device, semaphore, nullptr);
        m_renderFinished.clear();

        m_terrainVertices.Destroy();
        m_terrainIndices.Destroy();
        m_waterVertices.Destroy();
        m_palette.Destroy();
        m_atlasImage.Destroy();
        m_fontImage.Destroy();
        m_whiteImage.Destroy();
        m_terrainColor.Destroy();
        m_treeMask.Destroy();
        m_fieldMask.Destroy();
        m_ownerMask.Destroy();

        DestroyPipelines();

        if (m_descriptorPool) { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
        for (VkDescriptorSetLayout* layout : { &m_globalLayout, &m_textureLayout, &m_terrainLayout })
        {
            if (*layout) { vkDestroyDescriptorSetLayout(device, *layout, nullptr); *layout = VK_NULL_HANDLE; }
        }
        if (m_nearestSampler) { vkDestroySampler(device, m_nearestSampler, nullptr); m_nearestSampler = VK_NULL_HANDLE; }
        if (m_linearSampler) { vkDestroySampler(device, m_linearSampler, nullptr); m_linearSampler = VK_NULL_HANDLE; }

        m_swapchain.Destroy();
        vulkan.Shutdown();
        WOC_LOG_INFO("Renderer shut down");
    }
}
