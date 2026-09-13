// PartySetupScene.h - choose the map, the people, the banners.
//
// Every realm on the map is a seat, and every seat is set up the same way: a people (or
// "draw one") and a banner. One of them is the player's. That is the same shape the lobby
// works in, which is why a single party and a multiplayer one hand the generator the same
// document.
#pragma once

#include "IScene.h"
#include "../Game/Map/MapLoader.h"
#include "../Game/WorldGenerator.h"
#include "MapGenPanel.h"

namespace woc
{
    class PartySetupScene final : public IScene
    {
    public:
        SceneId Id() const override { return SceneId::PartySetup; }
        const char* Name() const override { return "PartySetup"; }

        void OnEnter() override;
        void OnExit() override;
        void Update(f32 deltaTime) override;
        void Render() override;

    private:
        void RenderMapList(const Rect& area);
        void RenderOptions(const Rect& area);
        void RenderSeats(const Rect& area);

        /// Pads or trims the seat list to the realm count and keeps the player's seat inside it.
        void EnsureSeats();
        void RandomiseColors();
        /// The colours a banner may be painted in, straight from game.json.
        const std::vector<u32>& Palette() const { return m_palette; }

        /// Reads each map's baked portrait and hands it to the renderer. Freed on the way
        /// out: a handful of descriptor sets is not something to leak between screens.
        void LoadThumbnails();
        void ReleaseThumbnails();

        std::vector<MapDescription> m_maps;
        std::vector<u32> m_thumbnails;   // one renderer handle per map, 0 where there is none
        std::vector<u32> m_palette;
        PartySettings m_settings;

        i32 m_selectedMap = 0;
        /// Which seat's people and banner the right-hand column is editing.
        i32 m_selectedSeat = 0;
        /// Which preset is highlighted and what is half-typed in the generator's fields.
        MapGenPanelState m_genPanel;

        std::string m_seedText = "0";
        f32 m_mapScroll = 0.0f;
        f32 m_seatScroll = 0.0f;
        std::string m_error;
    };
}
