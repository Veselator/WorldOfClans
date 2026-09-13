// EditorScene.h - map editor: generate a height map, paint terrain, place the world.
#pragma once

#include "IScene.h"
#include "MapGenPanel.h"
#include "../Core/Math.h"
#include "../Core/Json.h"
#include "../Game/Map/MapData.h"
#include "../Game/Map/MapGenerator.h"
#include "../Game/Map/MapLoader.h"
#include "../Game/World/SettlementDatabase.h"

#include <deque>
#include <vector>

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

    /// Which half of the opening window is showing: the maps that exist, or the generator.
    enum class BrowserTab
    {
        Open,
        Generate
    };

    /// One step back. The tiles are the map itself; the objects are everything standing on
    /// it. Both are kept, because a stroke of the terrain brush and the placing of a town
    /// are the same kind of edit as far as the designer's hand is concerned.
    struct EditorSnapshot
    {
        std::vector<Tile> tiles;
        u32 pixelWidth = 0;
        u32 pixelHeight = 0;
        u32 tilePixels = 4;
        Json objects;
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
        /// Files the current state away so Ctrl+Z can come back to it. Called once at the
        /// start of every edit, never in the middle of one - a stroke is one step, not one
        /// step per frame it is held.
        void PushUndo();
        void Undo();
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
        f32 m_brushRadius = 24.0f;
        f32 m_brushStrength = 0.6f;
        HeightMode m_heightMode = HeightMode::Sculpt;
        f32 m_heightTarget = 0.5f;
        SettlementKind m_settlementKind = SettlementKind::Village;
        i32 m_ownerSlot = 0;           // 0 = independent, otherwise editor clan index
        i32 m_raceIndex = 0;

        // The generator's parameters - the same structure, the same panel and the same
        // stored values the party screen uses, so the two cannot drift apart.
        MapGenSettings m_mapGen;
        MapGenPanelState m_genPanel;
        /// How many realms "one island per realm" should make, when the editor is asked
        /// for such a world. The editor has no party, so the designer says.
        i32 m_genRealms = 5;
        i32 m_genWidth = 1920;
        i32 m_genHeight = 1080;

        std::string m_mapName = "Нова карта";
        std::string m_folderName = "NewMap";
        /// The browser is up when the editor has nothing open, and whenever asked for.
        bool m_browserOpen = true;
        BrowserTab m_browserTab = BrowserTab::Open;
        f32 m_browserGenScroll = 0.0f;
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

        /// The way back. Bounded, because each step is the whole tile grid - a few megabytes
        /// on a large map - and a designer who needs forty steps back wants a saved file.
        std::deque<EditorSnapshot> m_undo;
        /// True while a mouse button is held, so one stroke files one step and not sixty.
        bool m_strokeOpen = false;
    };
}
