// PartySetupScene.h - choose the map, the people, the banners.
#pragma once

#include "IScene.h"
#include "../Game/Map/MapLoader.h"
#include "../Game/WorldGenerator.h"

namespace woc
{
    class PartySetupScene final : public IScene
    {
    public:
        SceneId Id() const override { return SceneId::PartySetup; }
        const char* Name() const override { return "PartySetup"; }

        void OnEnter() override;
        void Update(f32 deltaTime) override;
        void Render() override;

    private:
        void RenderMapList(const Rect& area);
        void RenderOptions(const Rect& area);
        void RenderColors(const Rect& area);

        void EnsureColorCount();
        void RandomiseColors();
        /// The colours a banner may be painted in, straight from game.json.
        const std::vector<u32>& Palette() const { return m_palette; }

        std::vector<MapDescription> m_maps;
        std::vector<u32> m_palette;
        PartySettings m_settings;

        i32 m_selectedMap = 0;
        i32 m_selectedRace = 0;
        /// -1 = the player's own banner, otherwise the index of a rival realm.
        i32 m_colorTarget = -1;

        std::string m_seedText = "0";
        f32 m_mapScroll = 0.0f;
        f32 m_colorScroll = 0.0f;
        std::string m_error;
    };
}
