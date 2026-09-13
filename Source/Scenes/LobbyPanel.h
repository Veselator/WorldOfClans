// LobbyPanel.h - the multiplayer front end, drawn inside the title screen.
//
// Two screens in one panel: the porch, where a player either opens a lobby or types the
// five-character code of one, and the room itself, where the table is laid - who sits
// where, which people and which banner each of them takes, what map is being played and
// whether everybody is ready.
#pragma once

#include "../Core/Math.h"
#include "../Game/Map/MapLoader.h"
#include "../Game/SaveGame.h"

#include <string>
#include <vector>

namespace woc
{
    class LobbyPanel
    {
    public:
        void Open();
        void Close();
        bool IsOpen() const { return m_open; }

        void Update(f32 deltaTime);
        void Draw();

    private:
        void DrawPorch(const Rect& panel);
        void DrawRoom(const Rect& panel);
        void DrawPlayers(const Rect& area);
        void DrawParty(const Rect& area);
        void DrawMySeat(const Rect& area);

        /// Reads the maps and saves this machine has, for the host's lists.
        void RefreshCatalogue();
        /// Host only: makes a fresh map, writes it into Maps/ and points the party at it,
        /// so that it is a real folder the moment anybody asks for it.
        void GenerateMapNow();

        bool m_open = false;
        /// True once a lobby exists (hosted or joined) and the room should be shown.
        bool m_inRoom = false;

        std::string m_playerName = "Князь";
        std::string m_lobbyName;
        /// A code, or the host's address typed in by hand.
        std::string m_codeText;
        f32 m_listScroll = 0.0f;
        std::string m_error;

        std::vector<MapDescription> m_maps;
        /// Scratch for asking whether a save's map still exists, so the list can say so.
        MapDescription m_probe;
        /// The code has just gone to the clipboard, and the button says so for a moment.
        bool m_codeCopied = false;
        f32 m_copiedTimer = 0.0f;
        std::vector<SaveSlot> m_saves;
        std::vector<u32> m_palette;
        /// Which world the "generate" button will make.
        i32 m_selectedPreset = 0;

        f32 m_playerScroll = 0.0f;
        f32 m_partyScroll = 0.0f;
        f32 m_seatScroll = 0.0f;
    };
}
