// GameScene.h - the game proper: the map, the orders and every information panel.
#pragma once

#include "IScene.h"

#include <array>
#include "../Core/Math.h"
#include "../Core/ImageIO.h"
#include "../Render/RenderTypes.h"
#include "../Game/World/SettlementDatabase.h"
#include "../Game/SaveGame.h"
#include "../Game/Systems/DiplomacySystem.h"
#include "../Core/Random.h"

#include <vector>
#include "../Game/World/World.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

namespace woc
{
    class Settlement;
    class Cohort;
    class Unit;
    class Character;
    class Clan;
    class State;

    enum class SelectionKind { None, Settlement, Cohort, Mine, BanditCamp };

    /// Which right-hand panel is showing.
    enum class PanelMode { Selection, Realm, Diplomacy, Chronicle };

    /// Which page of the settlement panel is open. The overview stays deliberately thin;
    /// everything a lord can *do* with a town lives one click away on its own page.
    enum class SettlementTab { Overview, Buildings, Recruit, Roads, Actions, Market };

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
        /// Bands every one of the player's armies inside the dragged rectangle.
        void SelectInBox(const Rect& box, bool additive);

        /// Picking works in screen space: an object's icon sits at the height of the ground
        /// it stands on, and unprojecting the cursor to a flat plane would miss it by more
        /// the flatter the camera angle is.
        EntityId PickSettlement(const Vec2& screenPoint, f32 screenRadius) const;
        EntityId PickCohort(const Vec2& screenPoint, f32 screenRadius) const;
        EntityId PickMine(const Vec2& screenPoint, f32 screenRadius) const;
        EntityId PickBanditCamp(const Vec2& screenPoint, f32 screenRadius) const;
        /// Cursor to map position, following the terrain rather than the z=0 plane.
        Vec2 ScreenToTerrain(const Vec2& screenPoint) const;

        // --- world drawing ---------------------------------------------------------------------
        void DrawWorld();
        void DrawSettlements();
        /// Towns in explored-but-unwatched country, drawn as the player last saw them.
        void DrawRememberedSettlements();
        void DrawCohorts();
        /// Supply and organisation, drawn as two thin bars beneath an army's banner.
        void DrawCohortBars();
        /// The head count, written inside the banner square under the race icon.
        void DrawCohortLabels();
        void DrawMines();
        /// The robbers' camps, and the bands that ride out of them.
        void DrawBanditCamps();
        void DrawSelectionMarkers();
        /// The rubber band, while the left button is being dragged across the map.
        void DrawSelectionBox();
        /// The fists thrown up over villages that have just risen: a scale that swells out
        /// of nothing, overshoots, and settles - the way a thing lands when it has weight.
        void DrawRevoltMarks(f32 deltaTime);
        void DrawSettlementLabels();
        void DrawOrderPreview();
        /// The short-lived lines struck between capitals when realms fall out or wed.
        void DrawDiplomaticFlares(f32 deltaTime);

        /// Puts the camera back over the player's own seat, zoomed in to read it.
        void FocusOnCapital();
        /// Chooses between the peaceful and the martial music by looking at the realm.
        void UpdateMusicMood();

        // --- interface ---------------------------------------------------------------------------
        void DrawTopBar();
        void DrawBottomBar();
        void DrawChronicle();
        void DrawSidePanel();
        void DrawSettlementPanel(const Rect& area, Settlement& settlement);
        void DrawSettlementOverview(const Rect& content, Settlement& settlement, f32& y);
        void DrawSettlementBuildings(const Rect& content, Settlement& settlement, f32& y);
        void DrawSettlementRecruit(const Rect& content, Settlement& settlement, f32& y);
        void DrawSettlementRoads(const Rect& content, Settlement& settlement, f32& y);
        void DrawSettlementActions(const Rect& content, Settlement& settlement, f32& y);
        /// The exchange floor: what each good fetches here today, and the counter to swap
        /// one for another at that rate.
        void DrawSettlementMarket(const Rect& content, Settlement& settlement, f32& y);
        void DrawCohortPanel(const Rect& area, Cohort& cohort);
        void DrawMinePanel(const Rect& area, MineSite& mine);
        /// What is known about a camp, and the order to burn it out.
        void DrawBanditCampPanel(const Rect& area, BanditCamp& camp);
        void DrawUnitDetails(const Rect& area, Unit& unit);
        void DrawCharacterDetails(const Rect& area, const Character& character);
        void DrawRealmPanel(const Rect& area);
        void DrawDiplomacyPanel(const Rect& area);
        void DrawChroniclePanel(const Rect& area);
        /// What a realm can raise on the map itself - seats and roads - as opposed to the
        /// improvements that go inside a settlement. Shown when nothing is selected.
        void DrawConstructionPanel(const Rect& area);
        void DrawNamingDialog();

        // --- the minimap ---------------------------------------------------------------------
        /// Where the little map sits. The side panel stops where this begins.
        Rect MinimapRect() const;
        /// Reads the map's baked portrait, baking one first if the map has none.
        void LoadMinimapBase();
        /// Repaints the picture when what it shows has changed, and not otherwise.
        void UpdateMinimap(f32 deltaTime);
        void RebuildMinimap();
        void DrawMinimap();

        // The geometry of each dialog, so the modal region can be declared at the top of a
        // frame - long before the dialog itself is drawn over everything else.
        Rect NamingDialogRect() const;
        Rect OfferDialogRect() const;
        Rect PauseMenuRect() const;
        Rect SaveDialogRect() const;
        Rect GameOverRect() const;
        /// True while something is waiting for an answer.
        bool HasOpenDialog() const;
        void DrawTooltipForHover();
        void DrawPauseMenu();
        /// The embassy waiting on an answer: a pact, a alliance or a match between houses.
        void DrawOfferDialog();
        /// News too big for the chronicle, shouted across the top of the screen.
        void DrawHeralds(f32 deltaTime);
        void DrawSaveDialog();
        void DrawGameOver();
        void PerformSave(const std::string& slotName);
        /// Writes the party out on its own clock, if the player has asked for it. Kept in a
        /// slot of its own so it never treads on a save made by hand.
        void UpdateAutosave();
        /// Runs the party's half of the network: on the host, the orders that have arrived
        /// and the snapshots that go back; on a client, the snapshot that has come in.
        void UpdateNetwork(f32 deltaTime);
        /// Sends an order through the command layer - straight into the world here, or to
        /// the host when this machine is a client.
        bool Order(const Json& command);
        /// True when this machine is only watching the host's simulation.
        bool IsNetworkGuest() const;

        Color ClanColor(EntityId clanId) const;
        /// True in a multiplayer party, where an order takes effect at a tick rather than at
        /// once - for the host exactly as for everybody else.
        bool OrdersDeferred() const;
        /// The embassy waiting on this player's answer, unless the answer is already sent.
        const DiplomaticOffer* PendingOffer() const;
        void AnswerOffer(bool accept);
        /// Where a role's small mark sits in the sprite sheet, in pixels (x, y, w, h).
        /// Read once from sprites/unitIcons and kept.
        const Vec4& UnitIconPixels(UnitRole role) const;
        /// Adds a cohort to the current band, or starts a new one.
        void SelectCohort(EntityId cohortId, bool additive);
        bool IsSelected(EntityId cohortId) const;
        /// Every selected army the player is actually allowed to command.
        std::vector<Cohort*> CommandableSelection();
        std::string FormatNumber(f32 value) const;
        /// The pace of the slowest unit in a host - the one the column keeps step with.
        f32 SlowestUnitSpeed(const Cohort& cohort) const;
        /// Head counts as they fit on a banner: 940, 1.2k, 12k.
        static std::string ShortCount(u32 value);

        // The services this scene talks to every frame. They are process-wide singletons
        // with a stable address, so the scene binds them once instead of resolving the
        // same instance dozens of times per frame.
        Renderer& m_renderer = Renderer::Get();
        UI& m_ui = UI::Get();
        World& m_world = World::Get();
        Input& m_input = Input::Get();
        const Theme& m_theme = Theme::Get();

        SelectionKind m_selectionKind = SelectionKind::None;
        EntityId m_selected = kInvalidId;
        /// Several armies under one order. `m_selected` is always the first of these.
        std::vector<EntityId> m_selectedCohorts;
        EntityId m_selectedUnit = kInvalidId;
        EntityId m_selectedCharacter = kInvalidId;
        EntityId m_hoveredSettlement = kInvalidId;
        EntityId m_hoveredCohort = kInvalidId;
        EntityId m_hoveredMine = kInvalidId;
        EntityId m_hoveredCamp = kInvalidId;
        EntityId m_diplomacyTarget = kInvalidId;

        PanelMode m_panelMode = PanelMode::Selection;
        SettlementTab m_settlementTab = SettlementTab::Overview;
        mutable std::array<Vec4, static_cast<size_t>(UnitRole::Count)> m_unitIcons{};
        mutable bool m_unitIconsLoaded = false;
        bool m_placingSettlement = false;
        /// Choosing where the peasants will put in a wood.
        bool m_placingForest = false;
        /// The rubber band: where the drag started, and whether one is under way at all.
        bool m_boxSelecting = false;
        Vec2 m_boxAnchor;
        /// The site chosen for a new seat, held while the player names it.
        bool m_namingOpen = false;
        Vec2 m_pendingSite;
        std::string m_pendingName;
        bool m_pauseMenuOpen = false;
        /// Which units the player has ticked for detaching into a new host.
        bool m_splitPickerOpen = false;
        std::vector<EntityId> m_splitUnits;
        /// The host whose disband button has been pressed once and is waiting on a second.
        EntityId m_disbandTarget = kInvalidId;
        bool m_saveDialogOpen = false;
        std::vector<SaveSlot> m_saves;
        std::string m_saveName;
        i32 m_selectedSave = -1;
        f32 m_saveScroll = 0.0f;
        std::string m_mapFolder = "Test";
        /// The game day the last automatic save was written on, so the interval is counted
        /// in the world's calendar rather than in seconds at the keyboard.
        i32 m_lastAutosaveDay = 0;
        bool m_desyncReported = false;
        i32 m_answeredOfferDay = -1;
        EntityId m_answeredOfferFrom = kInvalidId;
        /// For things that are this machine's alone, like a suggested town name: drawing
        /// them from the world's generator would put the machines out of step.
        Random m_localRandom;
        std::string m_panelKey;
        bool m_gameOver = false;
        bool m_victory = false;
        SettlementKind m_placingKind = SettlementKind::Village;

        /// The stretch of the world the minimap is currently showing, in map units.
        Rect m_minimapRegion{ 0.0f, 0.0f, 0.0f, 0.0f };
        /// The map's own portrait, baked when the map was saved: the land the minimap
        /// draws, so that nothing at run time has to read the full colour layer.
        ImageData m_minimapBase;
        std::vector<u8> m_minimapPixels;
        f32 m_minimapTimer = 0.0f;
        /// What the last composite was made of. While it holds, there is nothing to redo.
        u64 m_minimapDrawn = 0;

        // --- the market counter ----------------------------------------------------------
        ResourceType m_tradeGive = ResourceType::Wood;
        ResourceType m_tradeTake = ResourceType::Money;
        f32 m_tradeAmount = 50.0f;

        f32 m_sidePanelScroll = 0.0f;
        f32 m_realmScroll = 0.0f;
        f32 m_chronicleScroll = 0.0f;
        f32 m_pulse = 0.0f;
        std::string m_status;
        f32 m_statusTimer = 0.0f;
        /// Last frame's length, for effects that run on the screen's clock.
        f32 m_frameDelta = 0.0f;
        /// Each host's headcount as last drawn, and how much is left of the flash its number
        /// gives when men are lost.
        struct LossFlash { u32 strength = 0; size_t units = 0; f32 timer = 0.0f; };
        std::unordered_map<EntityId, LossFlash> m_lossFlash;
    };
}
