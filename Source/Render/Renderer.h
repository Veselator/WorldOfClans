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

        /// How large interface text is drawn. Every scale a caller passes is multiplied by
        /// this, and so is every measurement, so a panel laid out from TextWidth still fits
        /// the text that lands in it.
        void SetUIScale(f32 scale) { m_uiScale = scale > 0.1f ? scale : 1.0f; }
        f32 UIScale() const { return m_uiScale; }

        // --- map layers ---------------------------------------------------------------------
        void SetTerrainMesh(const TerrainMesh& mesh);
        /// Full-resolution colour layer straight from Terrain.png.
        void SetTerrainColor(const std::vector<u8>& colorRGBA, u32 width, u32 height);
        /// Density layers at simulation-grid resolution; safe to call every time they change.
        void SetTreeMask(const std::vector<u8>& mask, u32 width, u32 height);
        void SetFieldMask(const std::vector<u8>& mask, u32 width, u32 height);
        void SetOwnerMask(const std::vector<u8>& mask, u32 width, u32 height);
        /// The road layer, at the map's full pixel resolution so a track has crisp edges.
        /// 0 = no road, 128 = packed earth, 255 = bridge decking.
        void SetRoadMask(const std::vector<u8>& mask, u32 width, u32 height);
        /// What the player has seen: 0 = never, 128 = once, 255 = now.
        void SetFogMask(const std::vector<u8>& mask, u32 width, u32 height);
        /// Nearness of the coast, read by the sea pass so the surf breaks along the shore.
        void SetShoreMask(const std::vector<u8>& mask, u32 width, u32 height);
        /// The little picture of the world drawn in the corner. RGBA, rebuilt by the scene
        /// whenever what the player knows of the map changes.
        void SetMinimapImage(const std::vector<u8>& rgba, u32 width, u32 height);
        void SetFogEnabled(bool enabled) { m_fogEnabled = enabled; }
        bool FogEnabled() const { return m_fogEnabled; }
        void SetOwnerPalette(const std::vector<Color>& colors);
        void SetTerrainEnabled(bool enabled) { m_terrainEnabled = enabled; }
        /// Realm tint and frontier lines can be switched off to read the bare land.
        void SetBordersVisible(bool visible) { m_bordersVisible = visible; }
        bool BordersVisible() const { return m_bordersVisible; }
        /// Hard tile edges (the default, and what a map painted in pixels actually is) or a
        /// softened frontier that reads as a drawn line rather than as a staircase.
        void SetSmoothBorders(bool smooth) { m_smoothBorders = smooth; }
        bool SmoothBorders() const { return m_smoothBorders; }
        /// How hard the frontier lines are drawn; influence mode wants a softer seam.
        void SetBorderStrength(f32 strength) { m_borderStrength = strength; }
        /// How strongly the thematic layer washes over the land. A political map is a hint;
        /// a map of peoples or of gods has to be read at a glance, so it tints much harder.
        void SetOwnerTint(f32 tint) { m_ownerTint = tint; }
        f32 DefaultOwnerTint() const { return m_defaultOwnerTint; }
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
        void UIAtlas(const Vec4& uvRect, const Rect& rect, const Color& tint);
        /// Draws the minimap picture into a rectangle. The image itself is whatever was
        /// last handed to SetMinimapImage.
        void UIMinimap(const Rect& rect, const Color& tint);

        /// Uploads a picture the interface can draw later - a map's baked portrait, say -
        /// and returns a handle to it. 0 means the upload failed. These live until the
        /// scene releases them, so a screen that lists maps should free its thumbnails
        /// when it leaves.
        u32 CreateUITexture(const std::vector<u8>& rgba, u32 width, u32 height);
        void ReleaseUITexture(u32 handle);
        void UIImage(u32 handle, const Rect& rect, const Color& tint);
        void UIText(const std::string& text, const Vec2& position, const Color& color, f32 scale = 1.0f);
        void UITextCentered(const std::string& text, const Rect& rect, const Color& color, f32 scale = 1.0f);
        f32 TextWidth(const std::string& text, f32 scale = 1.0f) const;
        f32 TextHeight(f32 scale = 1.0f) const { return m_font.LineHeight() * scale * m_uiScale; }

        void PushClip(const Rect& rect);
        void PopClip();

        /// UV rectangle of an atlas tile, needed by callers that build instances themselves.
        Vec4 SpriteUV(SpriteId sprite) const;
        /// UV rectangle of an arbitrary pixel rectangle of the sheet - for the small marks
        /// that do not sit on the 16-pixel grid.
        Vec4 AtlasUV(f32 x, f32 y, f32 width, f32 height) const;

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

        /// A picture a scene uploaded, with the descriptor the UI pass binds to draw it.
        struct UITexture
        {
            GpuImage image;
            VkDescriptorSet set = VK_NULL_HANDLE;
            bool alive = false;
        };

        struct UIDrawRange
        {
            UIDrawMode mode = UIDrawMode::Solid;
            u32 texture = 0;   // only for UIDrawMode::Image: which uploaded picture
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
        /// Queues a changed layer for the next frame's command buffer. Only a recreated
        /// image still goes the slow, synchronous way.
        void StreamMask(GpuImage& image, const std::vector<u8>& pixels, u32 width, u32 height,
                        VkFormat format, void (Renderer::*refresh)());
        /// Records every staged layer copy. Called once per frame, before the render pass.
        void RecordPendingUploads(VkCommandBuffer cmd);
        void RefreshShoreDescriptor();
        void RefreshMinimapDescriptor();
        void EnsureFrameCapacity(FrameData& frame, u32 spriteCount, u32 uiVertexCount);

        void RecordWater(VkCommandBuffer cmd, FrameData& frame);
        void RecordTerrain(VkCommandBuffer cmd, FrameData& frame);
        void RecordSprites(VkCommandBuffer cmd, FrameData& frame);
        void RecordUI(VkCommandBuffer cmd, FrameData& frame);

        void PushUIQuad(const Rect& rect, const Vec4& uv, const Color& color, UIDrawMode mode,
                        u32 texture = 0);
        void PushUITriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color, UIDrawMode mode);
        UIDrawRange& CurrentRange(UIDrawMode mode, u32 texture = 0);
        VkRect2D ActiveScissor() const;

        Window* m_window = nullptr;
        VulkanSwapchain m_swapchain;
        Camera m_camera;
        FontAtlas m_font;

        std::array<FrameData, kFramesInFlight> m_frames{};
        std::vector<VkSemaphore> m_renderFinished;   // one per swapchain image
        u32 m_frameIndex = 0;
        u32 m_imageIndex = 0;
        /// Rises once per presented frame. Streamed uploads use it to tell when the GPU
        /// has certainly finished with a staging buffer.
        u64 m_frameCounter = 0;
        std::vector<GpuImage*> m_streaming;
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
        GpuImage m_roadMask;
        GpuImage m_fogMask;
        GpuImage m_shoreMask;
        GpuImage m_minimapImage;
        GpuBuffer m_palette;

        VkDescriptorSet m_atlasSet = VK_NULL_HANDLE;
        VkDescriptorSet m_fontSet = VK_NULL_HANDLE;
        VkDescriptorSet m_whiteSet = VK_NULL_HANDLE;
        VkDescriptorSet m_terrainSet = VK_NULL_HANDLE;
        VkDescriptorSet m_shoreSet = VK_NULL_HANDLE;
        VkDescriptorSet m_minimapSet = VK_NULL_HANDLE;
        std::vector<UITexture> m_uiTextures;

        GpuBuffer m_terrainVertices;
        GpuBuffer m_terrainIndices;
        u32 m_terrainIndexCount = 0;
        bool m_terrainEnabled = true;
        bool m_bordersVisible = true;
        bool m_fogEnabled = false;
        Vec2 m_mapSize{ 0.0f, 0.0f };
        f32 m_borderStrength = 0.92f;
        bool m_smoothBorders = false;
        bool m_terrainTexturesReady = false;

        std::vector<SpriteInstance> m_sprites;
        std::vector<UIVertex> m_uiVertices;
        std::vector<UIDrawRange> m_uiRanges;
        std::vector<Rect> m_clipStack;

        std::vector<Vec4> m_atlasUVs;
        Vec2 m_atlasSize{ 1.0f, 1.0f };
        Color m_clearColor{ 0.02f, 0.03f, 0.05f, 1.0f };
        f32 m_uiScale = 1.0f;
        f32 m_deltaTime = 0.0f;
        f32 m_fps = 0.0f;
        f64 m_lastFrameTime = 0.0;
        f32 m_elapsed = 0.0f;
        f32 m_forestTiling = 40.0f;
        f32 m_fieldTiling = 60.0f;
        f32 m_ownerTint = 0.28f;
        f32 m_defaultOwnerTint = 0.28f;
        f32 m_borderWidth = 1.5f;
    };
}
