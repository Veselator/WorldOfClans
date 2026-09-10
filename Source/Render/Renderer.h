// Renderer.h - the single façade every scene draws through.
//
// Three pipelines cover the whole game: the displaced terrain grid, instanced world
// billboards, and a 2D batch for the interface. Scenes never touch Vulkan directly;
// they submit sprites and UI primitives and the renderer orders, batches and draws them.
#pragma once

#include "VulkanCommon.h"
#include "VulkanSwapchain.h"
#include "VulkanBuffer.h"
#include "Camera.h"
#include "FontAtlas.h"
#include "RenderTypes.h"
#include "../Core/Singleton.h"

#include <array>

namespace woc
{
    class Window;

    struct TerrainMesh
    {
        std::vector<TerrainVertex> vertices;
        std::vector<u32> indices;
    };

    class Renderer final : public Singleton<Renderer>
    {
        friend class Singleton<Renderer>;
    public:
        void Initialise(Window& window);
        void Shutdown();

        /// Starts the frame. Returns false when the frame must be skipped (minimised window).
        bool BeginFrame();
        void EndFrame();
        void OnResize(u32 width, u32 height);

        Camera& GetCamera() { return m_camera; }
        const Camera& GetCamera() const { return m_camera; }
        const FontAtlas& Font() const { return m_font; }
        Vec2 ViewportSize() const;
        f32 DeltaTime() const { return m_deltaTime; }
        f32 FramesPerSecond() const { return m_fps; }

        void SetClearColor(const Color& color) { m_clearColor = color; }

        // --- map layers ---------------------------------------------------------------------
        void SetTerrainMesh(const TerrainMesh& mesh);
        /// Full-resolution colour layer straight from Terrain.png.
        void SetTerrainColor(const std::vector<u8>& colorRGBA, u32 width, u32 height);
        /// Density layers at simulation-grid resolution; safe to call every time they change.
        void SetTreeMask(const std::vector<u8>& mask, u32 width, u32 height);
        void SetFieldMask(const std::vector<u8>& mask, u32 width, u32 height);
        void SetOwnerMask(const std::vector<u8>& mask, u32 width, u32 height);
        void SetOwnerPalette(const std::vector<Color>& colors);
        void SetTerrainEnabled(bool enabled) { m_terrainEnabled = enabled; }
        /// Realm tint and frontier lines can be switched off to read the bare land.
        void SetBordersVisible(bool visible) { m_bordersVisible = visible; }
        bool BordersVisible() const { return m_bordersVisible; }
        /// Lays the sea plane out around the map so the world has no visible edge.
        void SetWaterPlane(const Vec2& mapSize, f32 margin);

        // --- world sprites -------------------------------------------------------------------
        void DrawSprite(SpriteId sprite, const Vec2& mapPosition, f32 height, f32 worldSize,
                        const Color& tint, f32 anchor = 0.0f, f32 flash = 0.0f);
        void DrawSpriteRaw(const SpriteInstance& instance);

        // --- interface ----------------------------------------------------------------------
        void UIRect(const Rect& rect, const Color& color);
        void UIRectOutline(const Rect& rect, const Color& color, f32 thickness = 1.0f);
        void UILine(const Vec2& from, const Vec2& to, const Color& color, f32 thickness = 1.0f);
        void UISprite(SpriteId sprite, const Rect& rect, const Color& tint);
        void UIText(const std::string& text, const Vec2& position, const Color& color, f32 scale = 1.0f);
        void UITextCentered(const std::string& text, const Rect& rect, const Color& color, f32 scale = 1.0f);
        f32 TextWidth(const std::string& text, f32 scale = 1.0f) const;
        f32 TextHeight(f32 scale = 1.0f) const { return m_font.LineHeight() * scale; }

        void PushClip(const Rect& rect);
        void PopClip();

        /// UV rectangle of an atlas tile, needed by callers that build instances themselves.
        Vec4 SpriteUV(SpriteId sprite) const;

    private:
        Renderer() = default;
        ~Renderer() = default;

        static constexpr u32 kFramesInFlight = 2;

        struct FrameData
        {
            VkCommandBuffer command = VK_NULL_HANDLE;
            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            VkFence inFlight = VK_NULL_HANDLE;
            GpuBuffer globals;
            GpuBuffer spriteInstances;
            GpuBuffer uiVertices;
            VkDescriptorSet globalSet = VK_NULL_HANDLE;
            u32 spriteCapacity = 0;
            u32 uiCapacity = 0;
        };

        struct UIDrawRange
        {
            UIDrawMode mode = UIDrawMode::Solid;
            u32 first = 0;
            u32 count = 0;
            VkRect2D scissor{};
        };

        void CreateSamplers();
        void CreateDescriptorInfrastructure();
        void CreatePipelines();
        void CreateFrameResources();
        void CreateDefaultTextures();
        void DestroyPipelines();

        VkDescriptorSet CreateTextureSet(VkImageView view, VkSampler sampler);
        /// Recreates a single-channel layer only when its size actually changed.
        void UpdateMask(GpuImage& image, const std::vector<u8>& mask, u32 width, u32 height);
        void RefreshTerrainDescriptor();
        void EnsureFrameCapacity(FrameData& frame, u32 spriteCount, u32 uiVertexCount);

        void RecordWater(VkCommandBuffer cmd, FrameData& frame);
        void RecordTerrain(VkCommandBuffer cmd, FrameData& frame);
        void RecordSprites(VkCommandBuffer cmd, FrameData& frame);
        void RecordUI(VkCommandBuffer cmd, FrameData& frame);

        void PushUIQuad(const Rect& rect, const Vec4& uv, const Color& color, UIDrawMode mode);
        void PushUITriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color, UIDrawMode mode);
        UIDrawRange& CurrentRange(UIDrawMode mode);
        VkRect2D ActiveScissor() const;

        Window* m_window = nullptr;
        VulkanSwapchain m_swapchain;
        Camera m_camera;
        FontAtlas m_font;

        std::array<FrameData, kFramesInFlight> m_frames{};
        std::vector<VkSemaphore> m_renderFinished;   // one per swapchain image
        u32 m_frameIndex = 0;
        u32 m_imageIndex = 0;
        bool m_frameActive = false;
        bool m_needsResize = false;

        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_globalLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_textureLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_terrainLayout = VK_NULL_HANDLE;

        VkPipelineLayout m_waterPipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_waterPipeline = VK_NULL_HANDLE;
        GpuBuffer m_waterVertices;
        u32 m_waterVertexCount = 0;

        VkPipelineLayout m_terrainPipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_spritePipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_uiPipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_terrainPipeline = VK_NULL_HANDLE;
        VkPipeline m_spritePipeline = VK_NULL_HANDLE;
        VkPipeline m_uiPipeline = VK_NULL_HANDLE;

        VkSampler m_nearestSampler = VK_NULL_HANDLE;
        VkSampler m_linearSampler = VK_NULL_HANDLE;

        GpuImage m_atlasImage;
        GpuImage m_fontImage;
        GpuImage m_whiteImage;
        GpuImage m_terrainColor;
        GpuImage m_treeMask;
        GpuImage m_fieldMask;
        GpuImage m_ownerMask;
        GpuBuffer m_palette;

        VkDescriptorSet m_atlasSet = VK_NULL_HANDLE;
        VkDescriptorSet m_fontSet = VK_NULL_HANDLE;
        VkDescriptorSet m_whiteSet = VK_NULL_HANDLE;
        VkDescriptorSet m_terrainSet = VK_NULL_HANDLE;

        GpuBuffer m_terrainVertices;
        GpuBuffer m_terrainIndices;
        u32 m_terrainIndexCount = 0;
        bool m_terrainEnabled = true;
        bool m_bordersVisible = true;
        bool m_terrainTexturesReady = false;

        std::vector<SpriteInstance> m_sprites;
        std::vector<UIVertex> m_uiVertices;
        std::vector<UIDrawRange> m_uiRanges;
        std::vector<Rect> m_clipStack;

        std::vector<Vec4> m_atlasUVs;
        Color m_clearColor{ 0.02f, 0.03f, 0.05f, 1.0f };
        f32 m_deltaTime = 0.0f;
        f32 m_fps = 0.0f;
        f64 m_lastFrameTime = 0.0;
        f32 m_elapsed = 0.0f;
        f32 m_forestTiling = 40.0f;
        f32 m_fieldTiling = 60.0f;
        f32 m_ownerTint = 0.28f;
        f32 m_borderWidth = 1.5f;
    };
}
