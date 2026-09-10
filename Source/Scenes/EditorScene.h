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
        Forest,
        Field,
        Road,
        Settlement,
        Mine,
        Erase,
        Inspect
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
        void ApplyBrush(const Vec2& mapPosition, bool erase);
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

        MapDescription m_description;
        std::vector<MapDescription> m_maps;

        EditorTool m_tool = EditorTool::Terrain;
        i32 m_terrainIndex = 3;
        i32 m_brushRadius = 24;
        f32 m_brushStrength = 0.6f;
        SettlementKind m_settlementKind = SettlementKind::Village;
        i32 m_ownerSlot = 0;           // 0 = independent, otherwise editor clan index
        i32 m_raceIndex = 0;

        // Procedural generation parameters, all live-editable.
        i32 m_genSeed = 20250910;
        std::string m_seedText = "20250910";
        f32 m_genScale = 3.2f;
        f32 m_genSeaLevel = 0.42f;
        f32 m_genMountains = 0.78f;
        f32 m_genForest = 0.45f;
        i32 m_genWidth = 1920;
        i32 m_genHeight = 1080;

        std::string m_mapName = "Нова карта";
        std::string m_folderName = "NewMap";
        std::string m_message;
        f32 m_messageTimer = 0.0f;

        f32 m_listScroll = 0.0f;
        bool m_dirtyColor = false;
        f32 m_meshRefreshTimer = 0.0f;
        EntityId m_inspected = kInvalidId;
    };
}
