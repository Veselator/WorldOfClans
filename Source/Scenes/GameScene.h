// GameScene.h - the game proper: the map, the orders and every information panel.
#pragma once

#include "IScene.h"
#include "../Core/Math.h"
#include "../Render/RenderTypes.h"
#include "../Game/World/SettlementDatabase.h"
#include "../Game/SaveGame.h"

namespace woc
{
    class Settlement;
    class Cohort;
    class Unit;
    class Character;
    class Clan;
    class State;

    enum class SelectionKind { None, Settlement, Cohort };

    /// Which right-hand panel is showing.
    enum class PanelMode { Selection, Realm, Diplomacy, Chronicle };

    class GameScene final : public IScene
    {
    public:
        SceneId Id() const override { return SceneId::Game; }
        const char* Name() const override { return "Game"; }

        void OnEnter() override;
        void OnExit() override;
        void Update(f32 deltaTime) override;
        void Render() override;

    private:
        // --- input --------------------------------------------------------------------------
        void UpdateCamera(f32 deltaTime);
        void UpdateSelection();
        void UpdateHotkeys();
        void IssueOrder(const Vec2& mapPosition);

        /// Picking works in screen space: an object's icon sits at the height of the ground
        /// it stands on, and unprojecting the cursor to a flat plane would miss it by more
        /// the flatter the camera angle is.
        EntityId PickSettlement(const Vec2& screenPoint, f32 screenRadius) const;
        EntityId PickCohort(const Vec2& screenPoint, f32 screenRadius) const;
        /// Cursor to map position, following the terrain rather than the z=0 plane.
        Vec2 ScreenToTerrain(const Vec2& screenPoint) const;

        // --- world drawing ---------------------------------------------------------------------
        void DrawWorld();
        void DrawSettlements();
        void DrawCohorts();
        void DrawMines();
        void DrawSelectionMarkers();
        void DrawSettlementLabels();
        void DrawOrderPreview();

        // --- interface ---------------------------------------------------------------------------
        void DrawTopBar();
        void DrawBottomBar();
        void DrawChronicle();
        void DrawSidePanel();
        void DrawSettlementPanel(const Rect& area, Settlement& settlement);
        void DrawCohortPanel(const Rect& area, Cohort& cohort);
        void DrawUnitDetails(const Rect& area, Unit& unit);
        void DrawCharacterDetails(const Rect& area, const Character& character);
        void DrawRealmPanel(const Rect& area);
        void DrawDiplomacyPanel(const Rect& area);
        void DrawChroniclePanel(const Rect& area);
        void DrawTooltipForHover();
        void DrawPauseMenu();
        void DrawSaveDialog();
        void DrawGameOver();
        void PerformSave(const std::string& slotName);

        Color ClanColor(EntityId clanId) const;
        std::string FormatNumber(f32 value) const;

        SelectionKind m_selectionKind = SelectionKind::None;
        EntityId m_selected = kInvalidId;
        EntityId m_selectedUnit = kInvalidId;
        EntityId m_selectedCharacter = kInvalidId;
        EntityId m_hoveredSettlement = kInvalidId;
        EntityId m_hoveredCohort = kInvalidId;
        EntityId m_diplomacyTarget = kInvalidId;

        PanelMode m_panelMode = PanelMode::Selection;
        bool m_placingSettlement = false;
        bool m_pauseMenuOpen = false;
        bool m_saveDialogOpen = false;
        std::vector<SaveSlot> m_saves;
        std::string m_saveName;
        i32 m_selectedSave = -1;
        f32 m_saveScroll = 0.0f;
        std::string m_mapFolder = "Test";
        std::string m_panelKey;
        bool m_gameOver = false;
        bool m_victory = false;
        SettlementKind m_placingKind = SettlementKind::Village;

        f32 m_sidePanelScroll = 0.0f;
        f32 m_realmScroll = 0.0f;
        f32 m_chronicleScroll = 0.0f;
        f32 m_pulse = 0.0f;
        std::string m_status;
        f32 m_statusTimer = 0.0f;
    };
}
