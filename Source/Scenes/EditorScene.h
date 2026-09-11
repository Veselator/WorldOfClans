// EditorScene.h - map editor: generate a height map, paint terrain, place the world.
#pragma once

#include "IScene.h"
#include "../Core/Math.h"
#include "../Game/Map/MapData.h"
#include "../Game/Map/MapLoader.h"
#include "../Game/World/SettlementDatabase.h"

namespace woc
{
    enum class EditorTool
    {
        Terrain,
        Height,
        Forest,
        Field,
        Road,
        Settlement,
        Mine,
        Erase,
        Inspect
    };
    constexpr int kEditorToolCount = 9;

    /// How the height brush works. Levelling to a chosen value is what you want for a
    /// plateau or a lake bed; pushing the ground up and down by feel is what you want
    /// for everything else.
    enum class HeightMode
    {
        Level,   // drive the ground towards a chosen height
        Sculpt   // raise with the left button, lower with the right
    };

    class EditorScene final : public IScene
    {
    public:
        SceneId Id() const override { return SceneId::Editor; }
        const char* Name() const override { return "Editor"; }

        void OnEnter() override;
        void OnExit() override;
        void Update(f32 deltaTime) override;
        void Render() override;

    private:
        void UpdateCamera(f32 deltaTime);
        /// Cursor to map position, following the terrain rather than the z=0 plane.
        Vec2 ScreenToTerrain(const Vec2& screenPoint) const;
        void ApplyBrush(const Vec2& mapPosition, bool erase);
        /// Marks a square of tiles as needing their colour re-rasterised.
        void TouchRegion(const Coord& center, i32 span);
        /// Re-rasterises only what the brush actually touched.
        void FlushEdits();
        void PlaceSettlement(const Vec2& mapPosition);
        void PlaceMine(const Vec2& mapPosition);
        void RemoveAt(const Vec2& mapPosition);

        void GenerateTerrain();
        void RebuildColorLayer();
        void PushToRenderer(bool rebuildMesh);

        bool LoadMap(const std::string& folder);
        bool SaveMap();

        void DrawToolbar();
        void DrawInspector();
        void DrawObjects();
        /// The window the editor opens on: what already exists, and a way to start anew.
        void DrawMapBrowser();
        /// Each map's baked portrait, so the browser shows worlds rather than a list of
        /// folder names. Released when the editor closes.
        void LoadThumbnails();
        void ReleaseThumbnails();
        /// A dashed white outline standing on the ground along the map's four edges.
        void DrawMapBounds();

        /// Re-shapes the current map to the size in the panel, keeping what is painted.
        void ResizeMap();
        bool CreateMap();

        MapDescription m_description;
        std::vector<MapDescription> m_maps;
        std::vector<u32> m_thumbnails;   // one renderer handle per map, 0 where there is none

        EditorTool m_tool = EditorTool::Terrain;
        i32 m_terrainIndex = 3;
        i32 m_brushRadius = 24;
        f32 m_brushStrength = 0.6f;
        HeightMode m_heightMode = HeightMode::Sculpt;
        f32 m_heightTarget = 0.5f;
        SettlementKind m_settlementKind = SettlementKind::Village;
        i32 m_ownerSlot = 0;           // 0 = independent, otherwise editor clan index
        i32 m_raceIndex = 0;

        // Procedural generation parameters, all live-editable.
        i32 m_genSeed = 20250910;
        std::string m_seedText = "20250910";
        bool m_randomSeed = true;
        f32 m_genScale = 3.2f;
        f32 m_genSeaLevel = 0.42f;
        f32 m_genMountains = 0.78f;
        f32 m_genForest = 0.45f;
        i32 m_genWidth = 1920;
        i32 m_genHeight = 1080;

        std::string m_mapName = "Нова карта";
        std::string m_folderName = "NewMap";
        /// The browser is up when the editor has nothing open, and whenever asked for.
        bool m_browserOpen = true;
        f32 m_browserScroll = 0.0f;
        std::string m_newName = "Нова карта";
        std::string m_newFolder = "NewMap";
        i32 m_newWidth = 1920;
        i32 m_newHeight = 1080;
        std::string m_message;
        f32 m_messageTimer = 0.0f;

        f32 m_listScroll = 0.0f;
        bool m_dirtyColor = false;
        bool m_dirtyHeight = false;
        /// The tile rectangle the brush has touched since the last flush. Re-rasterising
        /// the whole colour layer for one stroke is what made the editor crawl.
        Coord m_dirtyMin{ 0, 0 };
        Coord m_dirtyMax{ -1, -1 };
        f32 m_meshRefreshTimer = 0.0f;
        EntityId m_inspected = kInvalidId;
    };
}
