#include "GameScene.h"
#include "SceneManager.h"

#include "../Audio/AudioSystem.h"
#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Game/Systems/BattleSystem.h"
#include "../Game/Factories/NamePool.h"
#include "../Game/Factories/SettlementFactory.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/Systems/DiplomacySystem.h"
#include "../Game/Systems/FogSystem.h"
#include "../Game/Systems/MarketSystem.h"
#include "../Game/Systems/MovementSystem.h"
#include "../Game/Systems/PoliticsSystem.h"
#include "../Game/Systems/SettlementSystem.h"
#include "../Game/Systems/Simulation.h"
#include "../Game/World/RaceDatabase.h"
#include "../Game/World/World.h"
#include "../Game/WorldGenerator.h"
#include "../Core/Random.h"
#include "../Core/Settings.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace woc
{
    namespace
    {
        /// Lifts a banner colour to something readable as text on a dark plate. Some clan
        /// colours are deep enough that a name painted in them is a smudge.
        Color LabelColor(const Color& banner)
        {
            const Color bright = LerpColor(banner, Color(1.0f, 1.0f, 1.0f, 1.0f), 0.45f);
            const f32 luminance = bright.r * 0.299f + bright.g * 0.587f + bright.b * 0.114f;
            if (luminance >= 0.62f) return bright;

            const f32 lift = 0.62f / std::max(0.05f, luminance);
            return { std::min(1.0f, bright.r * lift),
                     std::min(1.0f, bright.g * lift),
                     std::min(1.0f, bright.b * lift), 1.0f };
        }

        std::string Percent(f32 value01)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", Clamp01(value01) * 100.0f);
            return buffer;
        }
    }

    void GameScene::OnEnter()
    {
        SceneManager& scenes = SceneManager::Get();

        m_renderer.SetTerrainEnabled(true);
        m_renderer.SetClearColor(Color::FromRGB(0x00fbf2));   // open sea beyond the map, matching the lit water

        // The title screen hides the borders behind its backdrop; a party always starts on
        // the political map, with whatever the player chose in the settings.
        m_renderer.SetBordersVisible(Settings::Get().showBorders);
        CoverageSystem::Get().SetMode(MapMode::Realms, m_world);
        m_settlementTab = SettlementTab::Overview;

        const std::string saveFile = scenes.Payload("load");
        bool ready = false;

        if (!saveFile.empty())
        {
            ready = SaveGame::Load(m_world, saveFile, m_mapFolder);
            scenes.SetPayload("load", "");   // a later restart must not reload the same file
            if (ready)
            {
                WorldGenerator::PublishMapToRenderer(m_world);
                m_saveName = m_world.HumanState() ? m_world.HumanState()->name : std::string("Гра");
            }
        }
        else
        {
            const PartySettings settings = PartySettings::FromJson(scenes.Data("party"));
            m_mapFolder = settings.mapFolder;
            ready = WorldGenerator::Generate(m_world, settings);
            if (ready) m_saveName = m_world.HumanState() ? m_world.HumanState()->name : std::string("Гра");
        }

        if (!ready)
        {
            WOC_LOG_ERROR("Could not start the game; returning to the menu");
            scenes.Request(SceneId::MainMenu);
            return;
        }

        LoadMinimapBase();

        FocusOnCapital();
    }

    void GameScene::UpdateMusicMood()
    {
        // Only the player's own wars change the music. A quarrel on the far side of the map
        // between two neighbours is not his business and should not be his soundtrack.
        const State* state = m_world.HumanState();
        bool atWar = false;
        if (state)
        {
            for (const auto& [stateId, other] : m_world.States())
            {
                if (stateId == state->id || other.eliminated) continue;
                if (state->IsAtWarWith(stateId)) { atWar = true; break; }
            }
        }

        AudioSystem::Get().SetMood(atWar ? MusicMood::War : MusicMood::Calm);
    }

    void GameScene::FocusOnCapital()
    {
        const State* state = m_world.HumanState();
        const Settlement* seat = state ? m_world.CapitalOf(state->id) : nullptr;
        if (!seat) return;

        Camera& camera = m_renderer.GetCamera();
        camera.SetZoom(ConfigManager::Get().Float("camera/startZoom", 2.2f));
        camera.SetFocus(seat->position);
        camera.ClampToBounds();

        m_selectionKind = SelectionKind::Settlement;
        m_selected = seat->id;
        m_selectedCohorts.clear();
        m_settlementTab = SettlementTab::Overview;
        m_panelMode = PanelMode::Selection;
    }

    void GameScene::OnExit()
    {
        m_world.Reset();
    }

    std::string GameScene::ShortCount(u32 value)
    {
        char buffer[16];
        if (value < 1000)
        {
            std::snprintf(buffer, sizeof(buffer), "%u", value);
        }
        else if (value < 10000)
        {
            // One decimal is worth keeping while the number is still small enough to read.
            std::snprintf(buffer, sizeof(buffer), "%.1fk", static_cast<f32>(value) / 1000.0f);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%uk", value / 1000);
        }
        return buffer;
    }

    Color GameScene::ClanColor(EntityId clanId) const
    {
        const Clan* clan = m_world.FindClan(clanId);
        return clan ? clan->color : Color::FromRGB(0x8B97A4);
    }

    std::string GameScene::FormatNumber(f32 value) const
    {
        char buffer[32];
        if (std::abs(value) >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        else std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        return buffer;
    }

    // =====================================================================================
    // Update
    // =====================================================================================

    void GameScene::Update(f32 deltaTime)
    {
        m_pulse += deltaTime;
        if (m_statusTimer > 0.0f) m_statusTimer -= deltaTime;

        if (PoliticsSystem::Get().CurrentOutcome() != Outcome::Playing)
        {
            m_gameOver = true;
            m_victory = PoliticsSystem::Get().CurrentOutcome() == Outcome::Victory;
        }

        UpdateMusicMood();
        UpdateHotkeys();
        // Repainted on its own clock, and outside Render(): the upload waits on the device,
        // which is not something to do in the middle of recording a frame.
        UpdateMinimap(deltaTime);
        // The world stands still behind a modal, and once the party is decided. An embassy
        // waiting on an answer counts: the player should not be asked to decide a treaty
        // while armies keep marching behind the dialog.
        if (HasOpenDialog()) return;

        UpdateCamera(deltaTime);
        UpdateSelection();

        Simulation::Get().Update(m_world, deltaTime);
    }

    void GameScene::UpdateCamera(f32 deltaTime)
    {
        ConfigManager& config = ConfigManager::Get();
        Camera& camera = m_renderer.GetCamera();

        const Vec2 viewport = m_renderer.ViewportSize();
        camera.SetViewport(viewport.x, viewport.y);

        if (m_ui.WantsKeyboard()) return;

        // Q and E spin the map, R puts it back the way it started.
        const f32 rotateSpeed = Settings::Get().rotateSpeed;
        if (m_input.IsKeyDown(Key::Q)) camera.RotateBy(-rotateSpeed * deltaTime);
        if (m_input.IsKeyDown(Key::E)) camera.RotateBy(rotateSpeed * deltaTime);
        if (m_input.WasKeyPressed(Key::R)) camera.ResetRotation();

        const f32 panSpeed = config.Float("camera/panSpeed", 900.0f) / camera.Zoom();
        Vec2 pan;
        if (m_input.IsKeyDown(Key::A) || m_input.IsKeyDown(Key::Left)) pan.x -= 1.0f;
        if (m_input.IsKeyDown(Key::D) || m_input.IsKeyDown(Key::Right)) pan.x += 1.0f;
        if (m_input.IsKeyDown(Key::W) || m_input.IsKeyDown(Key::Up)) pan.y -= 1.0f;
        if (m_input.IsKeyDown(Key::S) || m_input.IsKeyDown(Key::Down)) pan.y += 1.0f;

        // Edge scrolling, unless the pointer is over the interface.
        const f32 margin = Settings::Get().edgeScroll;
        const Vec2 mouse = m_input.MousePosition();
        if (!m_ui.WantsMouse() && margin > 0.0f)
        {
            if (mouse.x < margin) pan.x -= 1.0f;
            if (mouse.x > viewport.x - margin) pan.x += 1.0f;
            if (mouse.y < margin) pan.y -= 1.0f;
            if (mouse.y > viewport.y - margin) pan.y += 1.0f;
        }

        if (pan.LengthSq() > 0.0f)
        {
            // Screen-relative, so W is always "away from the viewer" however the map is turned.
            camera.PanScreenRelative(pan.Normalized() * (panSpeed * deltaTime));
        }

        const f32 wheel = m_input.WheelDelta();
        if (wheel != 0.0f && !m_ui.WantsMouse())
        {
            const f32 step = config.Float("camera/zoomStep", 1.12f);
            camera.ZoomAt(wheel > 0.0f ? step : 1.0f / step, mouse);
        }
    }

    void GameScene::UpdateHotkeys()
    {
        if (m_ui.WantsKeyboard()) return;

        Simulation& simulation = Simulation::Get();

        if (m_input.WasKeyPressed(Key::Space)) simulation.TogglePause();
        if (m_input.WasKeyPressed(Key::Plus)) simulation.SetSpeedIndex(simulation.SpeedIndex() + 1);
        if (m_input.WasKeyPressed(Key::Minus)) simulation.SetSpeedIndex(simulation.SpeedIndex() - 1);

        // A bare digit is the clock - 0 pauses, 1 to 5 pick a tempo. Holding shift turns the
        // same row into the four panels, so neither has to give the other its keys up.
        const bool shift = m_input.IsKeyDown(Key::Shift);
        for (i32 i = 0; i <= 5; ++i)
        {
            if (!m_input.WasKeyPressed(static_cast<Key>(static_cast<u16>(Key::Num0) + i))) continue;

            if (!shift) simulation.SetSpeedIndex(i);
            else if (i >= 1 && i <= 4) m_panelMode = static_cast<PanelMode>(i - 1);
        }

        // Inside a settlement, one key per page. They do nothing when the panel is showing
        // something else, which is what keeps them from being a trap.
        if (m_panelMode == PanelMode::Selection && m_selectionKind == SelectionKind::Settlement)
        {
            const Settlement* seat = m_world.FindSettlement(m_selected);
            const Clan* owner = seat ? m_world.FindClan(seat->owner) : nullptr;
            const State* human = m_world.HumanState();
            if (owner && human && owner->state == human->id)
            {
                struct Shortcut { Key key; SettlementTab tab; };
                static const Shortcut kPages[] = {
                    { Key::Z, SettlementTab::Buildings },
                    { Key::X, SettlementTab::Recruit },
                    { Key::C, SettlementTab::Roads },
                    { Key::V, SettlementTab::Actions },
                    { Key::B, SettlementTab::Market },
                };
                for (const Shortcut& page : kPages)
                {
                    if (!m_input.WasKeyPressed(page.key)) continue;
                    // No market, no market page: the key does nothing rather than opening
                    // an empty counter.
                    if (page.tab == SettlementTab::Market && !MarketSystem::HasMarket(*seat)) continue;
                    // The same key twice goes back to the overview.
                    m_settlementTab = m_settlementTab == page.tab ? SettlementTab::Overview : page.tab;
                    m_sidePanelScroll = 0.0f;
                }
            }
        }

        // Snapshot the living world back into the map folder, so a party you like becomes
        // the authored starting position for that map.
        if (m_input.WasKeyPressed(Key::F9))
        {
            m_status = WorldGenerator::SaveObjects(m_world, m_mapFolder)
                ? "MapObjects.json збережено для карти " + m_mapFolder
                : "Не вдалося зберегти MapObjects.json";
            m_statusTimer = 4.0f;
        }

        if (m_input.WasKeyPressed(Key::F1))
        {
            m_renderer.SetBordersVisible(!m_renderer.BordersVisible());
            m_status = m_renderer.BordersVisible() ? "Кордони: увімкнено" : "Кордони: вимкнено";
            m_statusTimer = 2.5f;
        }
        // F2..F4 switch what the coloured layer over the land is telling you. Pressing the
        // same key twice goes back to the political map, so one key is one thought.
        auto mapMode = [&](MapMode wanted)
        {
            CoverageSystem& coverage = CoverageSystem::Get();
            const MapMode next = coverage.Mode() == wanted ? MapMode::Realms : wanted;
            coverage.SetMode(next, m_world);
            m_renderer.SetBordersVisible(true);
            m_status = std::string("Карта: ") + CoverageSystem::ModeName(next);
            m_statusTimer = 2.5f;
        };
        if (m_input.WasKeyPressed(Key::H))
        {
            FocusOnCapital();
            m_status = "Стольний град";
            m_statusTimer = 2.0f;
        }

        if (m_input.WasKeyPressed(Key::F2)) mapMode(MapMode::Influence);
        if (m_input.WasKeyPressed(Key::F3)) mapMode(MapMode::Races);
        if (m_input.WasKeyPressed(Key::F4)) mapMode(MapMode::Faiths);

        if (m_input.WasKeyPressed(Key::Escape))
        {
            // Escape backs out one step at a time and only ever opens the menu; leaving a
            // running game is a deliberate choice made in that menu, never a stray keypress.
            if (DiplomacySystem::Get().HasOffer()) DiplomacySystem::Get().DeclineOffer(m_world);
            else if (m_namingOpen) { m_namingOpen = false; m_ui.RestartTransition("game.naming"); }
            else if (m_saveDialogOpen) m_saveDialogOpen = false;
            else if (m_pauseMenuOpen) m_pauseMenuOpen = false;
            else if (m_placingSettlement) m_placingSettlement = false;
            // A settlement page is a screen inside a screen: back out of it before letting
            // go of the settlement itself.
            else if (m_selectionKind == SelectionKind::Settlement &&
                     m_settlementTab != SettlementTab::Overview)
            {
                m_settlementTab = SettlementTab::Overview;
                m_sidePanelScroll = 0.0f;
            }
            else if (m_panelMode != PanelMode::Selection && m_selectionKind == SelectionKind::None)
            {
                m_panelMode = PanelMode::Selection;
            }
            else if (m_selectedCharacter != kInvalidId) m_selectedCharacter = kInvalidId;
            else if (m_selectedUnit != kInvalidId) m_selectedUnit = kInvalidId;
            else if (m_selectionKind != SelectionKind::None)
            {
                m_selectionKind = SelectionKind::None;
                m_selected = kInvalidId;
                m_selectedCohorts.clear();
            }
            else m_pauseMenuOpen = true;
        }
    }

    Vec2 GameScene::ScreenToTerrain(const Vec2& screenPoint) const
    {
        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();

        // The ground is not flat, so unproject onto z=0, sample the height there and try
        // again. Two refinements are plenty for the slopes this terrain has.
        Vec2 position = camera.ScreenToMap(screenPoint, 0.0f);
        for (int i = 0; i < 2; ++i)
        {
            position = camera.ScreenToMap(screenPoint, map.WorldHeightAtMap(position));
        }
        return position;
    }

    EntityId GameScene::PickSettlement(const Vec2& screenPoint, f32 screenRadius) const
    {
        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();

        EntityId best = kInvalidId;
        f32 bestDistance = screenRadius * screenRadius;

        for (const auto& [id, settlement] : m_world.Settlements())
        {
            // What is hidden cannot be clicked: a town in unexplored country is not there
            // as far as the player is concerned.
            if (!FogSystem::Get().IsKnown(m_world, settlement.position)) continue;

            const Vec2 screen = camera.MapToScreen(settlement.position,
                                                   map.WorldHeightAtMap(settlement.position));
            const f32 distance = DistanceSq(screen, screenPoint);
            if (distance < bestDistance) { bestDistance = distance; best = id; }
        }
        return best;
    }

    EntityId GameScene::PickCohort(const Vec2& screenPoint, f32 screenRadius) const
    {
        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();

        // The icon is drawn at world size, so its footprint on screen grows with the zoom.
        // Hit-testing against a fixed radius made the collider shrink away from the sprite;
        // testing against the sprite's own half-extent keeps the two the same thing.
        const f32 half = std::max(screenRadius,
            ConfigManager::Get().Float("render/spriteScale/cohort", 20.0f) * camera.Zoom() * 0.5f);

        EntityId best = kInvalidId;
        f32 bestDistance = 1e9f;

        for (const auto& [id, cohort] : m_world.Cohorts())
        {
            // Garrisons are commanded from the settlement panel, not from the map.
            if (cohort.garrisonOf != kInvalidId) continue;
            if (!FogSystem::Get().IsVisible(m_world, cohort.position)) continue;

            const Vec2 screen = camera.MapToScreen(cohort.position,
                                                   map.WorldHeightAtMap(cohort.position));
            if (std::abs(screen.x - screenPoint.x) > half) continue;
            if (std::abs(screen.y - screenPoint.y) > half) continue;

            const f32 distance = DistanceSq(screen, screenPoint);
            if (distance < bestDistance) { bestDistance = distance; best = id; }
        }
        return best;
    }

    bool GameScene::IsSelected(EntityId cohortId) const
    {
        return std::find(m_selectedCohorts.begin(), m_selectedCohorts.end(), cohortId) !=
               m_selectedCohorts.end();
    }

    void GameScene::SelectCohort(EntityId cohortId, bool additive)
    {
        if (!additive)
        {
            m_selectedCohorts.assign(1, cohortId);
        }
        else if (IsSelected(cohortId))
        {
            // Clicking a banded army again drops it, which is how one trims a selection.
            m_selectedCohorts.erase(std::remove(m_selectedCohorts.begin(), m_selectedCohorts.end(), cohortId),
                                    m_selectedCohorts.end());
            if (m_selectedCohorts.empty())
            {
                m_selectionKind = SelectionKind::None;
                m_selected = kInvalidId;
                return;
            }
            m_selected = m_selectedCohorts.front();
            return;
        }
        else
        {
            m_selectedCohorts.push_back(cohortId);
        }

        m_selectionKind = SelectionKind::Cohort;
        m_selected = m_selectedCohorts.front();
        m_selectedUnit = kInvalidId;
        m_selectedCharacter = kInvalidId;
        m_panelMode = PanelMode::Selection;
    }

    std::vector<Cohort*> GameScene::CommandableSelection()
    {
        std::vector<Cohort*> armies;
        const State* humanState = m_world.HumanState();
        if (!humanState) return armies;

        for (EntityId id : m_selectedCohorts)
        {
            Cohort* cohort = m_world.FindCohort(id);
            if (!cohort) continue;
            const Clan* clan = m_world.FindClan(cohort->clan);
            if (!clan || clan->state != humanState->id) continue;
            armies.push_back(cohort);
        }
        return armies;
    }

    EntityId GameScene::PickMine(const Vec2& screenPoint, f32 screenRadius) const
    {
        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();

        const f32 half = std::max(screenRadius,
            ConfigManager::Get().Float("render/spriteScale/quarry", 18.0f) * camera.Zoom() * 0.5f);

        EntityId best = kInvalidId;
        f32 bestDistance = 1e9f;

        for (const MineSite& mine : m_world.Mines())
        {
            if (!FogSystem::Get().IsKnown(m_world, mine.position)) continue;

            const Vec2 screen = camera.MapToScreen(mine.position, map.WorldHeightAtMap(mine.position));
            if (std::abs(screen.x - screenPoint.x) > half) continue;
            if (std::abs(screen.y - screenPoint.y) > half) continue;

            const f32 distance = DistanceSq(screen, screenPoint);
            if (distance < bestDistance) { bestDistance = distance; best = mine.id; }
        }
        return best;
    }

    void GameScene::UpdateSelection()
    {
        Camera& camera = m_renderer.GetCamera();

        const Vec2 screen = m_input.MousePosition();
        const Vec2 mapPosition = ScreenToTerrain(screen);
        const f32 pickRadius = ConfigManager::Get().Float("render/pickRadius", 20.0f);

        m_hoveredCohort = m_ui.WantsMouse() ? kInvalidId : PickCohort(screen, pickRadius);
        m_hoveredSettlement = (m_ui.WantsMouse() || m_hoveredCohort != kInvalidId)
            ? kInvalidId : PickSettlement(screen, pickRadius);
        m_hoveredMine = (m_ui.WantsMouse() || m_hoveredCohort != kInvalidId ||
                         m_hoveredSettlement != kInvalidId)
            ? kInvalidId : PickMine(screen, pickRadius);

        if (m_ui.WantsMouse()) return;

        if (m_input.WasMousePressed(MouseButton::Left))
        {
            if (m_placingSettlement)
            {
                // The site is chosen here; the name, and the founding itself, come from the
                // dialog. A seat the player had to pick a spot for deserves a name too.
                m_placingSettlement = false;

                const Clan* founder = m_world.HumanClan();
                const EntityId ground = CoverageSystem::Get().OwnerAt(m_world, mapPosition);
                if (founder && ground != founder->id)
                {
                    m_status = "Будувати можна лише у своїх володіннях";
                    m_statusTimer = 3.5f;
                    return;
                }
                if (!SettlementFactory::CanPlace(m_world, m_world.Map(), m_placingKind, mapPosition,
                                                 founder ? founder->id : kInvalidId))
                {
                    m_status = "Тут будувати не можна";
                    m_statusTimer = 3.0f;
                    return;
                }

                m_pendingSite = mapPosition;

                // A name is suggested from the pool so the field is never blank, and the
                // player types over it only when he has something better in mind.
                const Clan* clan = m_world.HumanClan();
                m_pendingName = clan
                    ? NamePool::Get().SettlementName(clan->raceId, GlobalRandom())
                    : std::string();
                m_namingOpen = true;
                m_ui.RestartTransition("game.naming");
                return;
            }

            // Shift or Ctrl bands armies together; a plain click starts over.
            const bool additive = m_input.IsKeyDown(Key::Shift) || m_input.IsKeyDown(Key::Control);

            if (m_hoveredCohort != kInvalidId)
            {
                SelectCohort(m_hoveredCohort, additive);
            }
            else if (m_hoveredSettlement != kInvalidId)
            {
                m_selectionKind = SelectionKind::Settlement;
                m_selected = m_hoveredSettlement;
                m_selectedCohorts.clear();
                m_settlementTab = SettlementTab::Overview;
                m_panelMode = PanelMode::Selection;
            }
            else if (m_hoveredMine != kInvalidId)
            {
                m_selectionKind = SelectionKind::Mine;
                m_selected = m_hoveredMine;
                m_selectedCohorts.clear();
                m_panelMode = PanelMode::Selection;
            }
            else if (!additive)
            {
                m_selectionKind = SelectionKind::None;
                m_selected = kInvalidId;
                m_selectedCohorts.clear();
            }
        }

        if (m_input.WasMousePressed(MouseButton::Right))
        {
            IssueOrder(mapPosition);
        }
    }

    void GameScene::IssueOrder(const Vec2& mapPosition)
    {
        if (m_selectionKind != SelectionKind::Cohort) return;

        // Orders may only be given to one's own armies, and they are given to all of them:
        // a band selected with Shift marches, besieges and intercepts as one.
        std::vector<Cohort*> armies = CommandableSelection();
        if (armies.empty()) return;

        MovementSystem& movement = MovementSystem::Get();
        const Vec2 screen = m_input.MousePosition();
        const bool wantsRaid = m_input.IsKeyDown(Key::Shift);

        auto report = [&](const std::string& text)
        {
            m_status = armies.size() > 1
                ? text + "  (" + std::to_string(armies.size()) + ")"
                : text;
            m_statusTimer = 3.0f;
        };

        // Several armies cannot stand on one point, so a band spreads into a small ring
        // around the destination instead of piling onto a single tile.
        auto spread = [&](const Vec2& centre, size_t index)
        {
            if (armies.size() < 2) return centre;
            const f32 angle = static_cast<f32>(index) / static_cast<f32>(armies.size()) * 6.2831853f;
            const f32 radius = 26.0f + static_cast<f32>(armies.size()) * 4.0f;
            return Vec2{ centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius };
        };

        const EntityId targetSettlement = PickSettlement(screen, 26.0f);
        if (targetSettlement != kInvalidId)
        {
            Settlement* settlement = m_world.FindSettlement(targetSettlement);
            if (settlement)
            {
                i32 ordered = 0;
                bool refusedRaid = false;
                bool atPeace = false;

                for (size_t i = 0; i < armies.size(); ++i)
                {
                    Cohort* cohort = armies[i];
                    const bool own = settlement->owner == cohort->clan;
                    const bool hostile = m_world.MayAttackSettlement(cohort->clan, settlement->id);

                    TaskType task = TaskType::Move;
                    if (own) task = TaskType::Garrison;
                    else if (hostile)
                    {
                        if (wantsRaid && !cohort->mayRaid) { refusedRaid = true; continue; }
                        task = wantsRaid ? TaskType::Raid : TaskType::Besiege;
                    }
                    else if (settlement->owner != kInvalidId)
                    {
                        // Peace is peace: a march up to the walls is all that is allowed.
                        atPeace = true;
                    }

                    const Vec2 destination = task == TaskType::Move
                        ? spread(settlement->position, i)
                        : settlement->position;

                    if (movement.OrderTask(m_world, cohort->id, task, destination, targetSettlement))
                    {
                        ++ordered;
                        if (i == 0) m_status = std::string(Task::TypeName(task)) + ": " + settlement->name;
                    }
                }

                if (ordered == 0)
                {
                    report(refusedRaid ? "Ці загони не грабують — увімкніть у їхніх панелях"
                                       : "Туди не пройти");
                }
                else
                {
                    if (atPeace) m_status = "З цим родом ми не воюємо";
                    report(m_status);
                }
                return;
            }
        }

        const EntityId targetCohort = PickCohort(screen, 22.0f);
        if (targetCohort != kInvalidId && !IsSelected(targetCohort))
        {
            const Cohort* enemy = m_world.FindCohort(targetCohort);
            if (enemy && m_world.AreHostile(armies.front()->clan, enemy->clan))
            {
                for (Cohort* cohort : armies)
                {
                    movement.OrderTask(m_world, cohort->id, TaskType::Attack, enemy->position,
                                       kInvalidId, targetCohort);
                }
                report("Перехоплення: " + enemy->DisplayName());
                return;
            }
        }

        i32 marching = 0;
        for (size_t i = 0; i < armies.size(); ++i)
        {
            if (movement.OrderMove(m_world, armies[i]->id, spread(mapPosition, i))) ++marching;
        }
        if (marching == 0) report("Туди не пройти");
        else report("Похід");
    }

    // =====================================================================================
    // Rendering
    // =====================================================================================

    void GameScene::Render()
    {
        // A dialog claims the mouse for the whole frame, including the panels behind it
        // that are drawn first. Without this the player could still press buttons on the
        // side panel through the dimmed backdrop.
        if (m_namingOpen)          m_ui.SetModalRegion(NamingDialogRect());
        else if (DiplomacySystem::Get().HasOffer()) m_ui.SetModalRegion(OfferDialogRect());
        else if (m_saveDialogOpen) m_ui.SetModalRegion(SaveDialogRect());
        else if (m_pauseMenuOpen)  m_ui.SetModalRegion(PauseMenuRect());
        else if (m_gameOver)       m_ui.SetModalRegion(GameOverRect());

        DrawWorld();

        DrawTopBar();
        DrawSidePanel();
        DrawMinimap();
        DrawBottomBar();
        DrawChronicle();
        DrawTooltipForHover();
        DrawNamingDialog();
        DrawOfferDialog();
        DrawPauseMenu();
        DrawSaveDialog();
        DrawGameOver();
        DrawHeralds(m_renderer.DeltaTime());
    }

    void GameScene::DrawGameOver()
    {
        if (!m_gameOver) return;


        const f32 fade = m_ui.Transition("game.over", true, 0.6f);
        const Vec2 viewport = m_renderer.ViewportSize();
        m_renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, m_theme.shadow.WithAlpha(0.78f * fade));

        Rect panel = GameOverRect();
        panel.y += (1.0f - fade) * 40.0f;
        m_ui.Panel(panel);
        m_ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const Color accent = m_victory ? m_theme.accent : m_theme.negative;
        m_renderer.UITextCentered(m_victory ? "ПЕРЕМОГА" : "ПОРАЗКА",
                                { panel.x, panel.y + 34.0f, panel.w, 44.0f }, accent, 2.2f);

        const State* state = m_world.HumanState();
        if (state)
        {
            m_renderer.UITextCentered(state->name, { panel.x, panel.y + 86.0f, panel.w, 24.0f },
                                    m_theme.textStrong);
        }

        m_ui.Paragraph({ panel.x + 40.0f, panel.y + 122.0f, panel.w - 80.0f, 0.0f },
                     PoliticsSystem::Get().Reason(), m_theme.text);

        m_renderer.UITextCentered(m_world.Time().ToString(),
                                { panel.x, panel.y + 174.0f, panel.w, 20.0f }, m_theme.textDim);

        const f32 buttonWidth = 220.0f;
        const f32 y = panel.Bottom() - 56.0f;
        if (m_ui.Button({ panel.Center().x - buttonWidth - 6.0f, y, buttonWidth, 34.0f }, "У головне меню"))
        {
            SceneManager::Get().Request(SceneId::MainMenu);
        }
        if (m_ui.Button({ panel.Center().x + 6.0f, y, buttonWidth, 34.0f }, "Вихід із гри"))
        {
            SceneManager::Get().RequestQuit();
        }
    }

    Rect GameScene::NamingDialogRect() const
    {
        const Vec2 viewport = m_renderer.ViewportSize();
        return { (viewport.x - 420.0f) * 0.5f, (viewport.y - 180.0f) * 0.5f, 420.0f, 180.0f };
    }

    Rect GameScene::PauseMenuRect() const
    {
        const Vec2 viewport = m_renderer.ViewportSize();
        return { (viewport.x - 340.0f) * 0.5f, (viewport.y - 330.0f) * 0.5f, 340.0f, 330.0f };
    }

    Rect GameScene::OfferDialogRect() const
    {
        const Vec2 viewport = m_renderer.ViewportSize();
        return { (viewport.x - 460.0f) * 0.5f, (viewport.y - 250.0f) * 0.5f, 460.0f, 250.0f };
    }

    Rect GameScene::SaveDialogRect() const
    {
        const Vec2 viewport = m_renderer.ViewportSize();
        return { (viewport.x - 520.0f) * 0.5f, (viewport.y - 420.0f) * 0.5f, 520.0f, 420.0f };
    }

    Rect GameScene::GameOverRect() const
    {
        const Vec2 viewport = m_renderer.ViewportSize();
        return { (viewport.x - 560.0f) * 0.5f, (viewport.y - 280.0f) * 0.5f, 560.0f, 280.0f };
    }

    bool GameScene::HasOpenDialog() const
    {
        return m_namingOpen || m_saveDialogOpen || m_pauseMenuOpen || m_gameOver ||
               DiplomacySystem::Get().HasOffer();
    }

    void GameScene::DrawHeralds(f32 deltaTime)
    {
        std::vector<Herald>& heralds = m_world.Heralds();
        if (heralds.empty()) return;

        const Vec2 viewport = m_renderer.ViewportSize();
        f32 y = m_theme.topBarHeight + 24.0f;

        for (Herald& herald : heralds)
        {
            herald.life -= deltaTime;
            if (herald.life <= 0.0f) continue;

            // Struck in hard, held, then let go: the eye catches the arrival and the news
            // does not sit on the screen once it has been read.
            const f32 age = 1.0f - herald.life / std::max(0.01f, herald.duration);
            const f32 fade = std::min(1.0f, std::min(age * 6.0f, (1.0f - age) * 4.0f));
            if (fade <= 0.0f) continue;

            const f32 titleScale = 2.4f;
            const f32 titleWidth = m_renderer.TextWidth(herald.headline, titleScale);
            const f32 detailWidth = m_renderer.TextWidth(herald.detail);
            const f32 width = std::max(titleWidth, detailWidth) + 60.0f;
            const f32 height = m_renderer.TextHeight(titleScale) + m_renderer.TextHeight() + 26.0f;

            const Rect banner{ (viewport.x - width) * 0.5f, y - (1.0f - fade) * 12.0f, width, height };
            m_renderer.UIRect(banner, m_theme.shadow.WithAlpha(0.72f * fade));
            m_renderer.UIRect({ banner.x, banner.y, banner.w, 2.0f }, herald.color.WithAlpha(fade));
            m_renderer.UIRect({ banner.x, banner.Bottom() - 2.0f, banner.w, 2.0f }, herald.color.WithAlpha(fade));

            m_renderer.UITextCentered(herald.headline,
                                      { banner.x, banner.y + 10.0f, banner.w, m_renderer.TextHeight(titleScale) },
                                      herald.color.WithAlpha(fade), titleScale);
            m_renderer.UITextCentered(herald.detail,
                                      { banner.x, banner.Bottom() - m_renderer.TextHeight() - 10.0f,
                                        banner.w, m_renderer.TextHeight() },
                                      m_theme.textStrong.WithAlpha(fade));
            y += height + 8.0f;
        }

        heralds.erase(std::remove_if(heralds.begin(), heralds.end(),
                                     [](const Herald& h) { return h.life <= 0.0f; }),
                      heralds.end());
    }

    void GameScene::DrawOfferDialog()
    {
        DiplomacySystem& diplomacy = DiplomacySystem::Get();
        if (!diplomacy.HasOffer()) return;

        const DiplomaticOffer& offer = diplomacy.FrontOffer();
        const State* asker = m_world.FindState(offer.from);
        if (!asker)
        {
            diplomacy.DeclineOffer(m_world);
            return;
        }

        const Vec2 viewport = m_renderer.ViewportSize();
        m_renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, m_theme.shadow.WithAlpha(0.6f));

        const Rect panel = OfferDialogRect();
        m_ui.Panel(panel, DiplomacySystem::OfferTitle(offer.kind));
        m_ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const f32 margin = 24.0f;
        f32 y = panel.y + m_theme.headerHeight + 16.0f;

        m_renderer.UITextCentered(asker->name, { panel.x, y, panel.w, 26.0f }, asker->color, 1.4f);
        y += 34.0f;

        m_ui.Paragraph({ panel.x + margin, y, panel.w - margin * 2.0f, 0.0f },
                       DiplomacySystem::OfferBody(offer.kind), m_theme.text);

        // How the other court feels about us, because that is what the answer turns on.
        const State* us = m_world.HumanState();
        if (us)
        {
            const f32 opinion = diplomacy.Opinion(m_world, us->id, asker->id);
            m_ui.KeyValue({ panel.x + margin, panel.Bottom() - 92.0f, panel.w - margin * 2.0f, 20.0f },
                          "Прихильність до нас", FormatNumber(opinion),
                          opinion >= 0.0f ? m_theme.positive : m_theme.negative);
        }

        const f32 buttonWidth = (panel.w - margin * 2.0f - 12.0f) * 0.5f;
        const f32 buttonY = panel.Bottom() - 56.0f;

        if (m_ui.Button({ panel.x + margin, buttonY, buttonWidth, 34.0f }, "Погодитись"))
        {
            diplomacy.AcceptOffer(m_world);
            m_status = "Угоду укладено";
            m_statusTimer = 3.0f;
        }
        if (m_ui.Button({ panel.x + margin + buttonWidth + 12.0f, buttonY, buttonWidth, 34.0f }, "Відмовити"))
        {
            diplomacy.DeclineOffer(m_world);
            m_status = "Посольству відмовлено";
            m_statusTimer = 3.0f;
        }
    }

    void GameScene::DrawPauseMenu()
    {
        const f32 fade = m_ui.Transition("game.pause", m_pauseMenuOpen, 0.15f);
        if (fade <= 0.001f) return;

        const Vec2 viewport = m_renderer.ViewportSize();
        m_renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, m_theme.shadow.WithAlpha(0.6f * fade));

        Rect panel = PauseMenuRect();
        panel.y += (1.0f - fade) * 26.0f;
        m_ui.Panel(panel, "Пауза");
        m_ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const f32 buttonWidth = 260.0f;
        const f32 x = panel.Center().x - buttonWidth * 0.5f;
        f32 y = panel.y + m_theme.headerHeight + 20.0f;

        auto button = [&](const std::string& label)
        {
            const Rect rect{ x, y, buttonWidth, 34.0f };
            y += 42.0f;
            return m_ui.Button(rect, label, !m_saveDialogOpen);
        };

        if (button("Продовжити")) m_pauseMenuOpen = false;

        if (button("Зберегти гру"))
        {
            // Overwrites the current slot straight away; the next button asks for a name.
            PerformSave(m_saveName);
            m_pauseMenuOpen = false;
        }

        if (button("Зберегти як..."))
        {
            m_saves = SaveGame::List();
            m_saveDialogOpen = true;
            m_ui.RestartTransition("game.savedialog");
        }

        if (button("Зберегти об'єкти карти (F9)"))
        {
            m_status = WorldGenerator::SaveObjects(m_world, m_mapFolder)
                ? "MapObjects.json збережено"
                : "Не вдалося зберегти";
            m_statusTimer = 4.0f;
            m_pauseMenuOpen = false;
        }

        if (button("У головне меню")) SceneManager::Get().Request(SceneId::MainMenu);
        if (button("Вихід із гри")) SceneManager::Get().RequestQuit();
    }

    void GameScene::PerformSave(const std::string& slotName)
    {
        const std::string clean = slotName.empty() ? std::string("Гра") : slotName;
        m_status = SaveGame::Save(m_world, clean, m_mapFolder)
            ? "Збережено: " + clean
            : "Не вдалося зберегти";
        m_statusTimer = 4.0f;
    }

    void GameScene::DrawSaveDialog()
    {
        const f32 fade = m_ui.Transition("game.savedialog", m_saveDialogOpen, 0.15f);
        if (fade <= 0.001f) return;

        const Vec2 viewport = m_renderer.ViewportSize();
        m_renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, m_theme.shadow.WithAlpha(0.7f * fade));

        Rect panel = SaveDialogRect();
        panel.y += (1.0f - fade) * 26.0f;
        m_ui.Panel(panel, "Зберегти як");
        m_ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const Rect body = Rect{ panel.x, panel.y + m_theme.headerHeight,
                                panel.w, panel.h - m_theme.headerHeight - 96.0f }.Inset(m_theme.padding);

        if (m_saves.empty())
        {
            m_ui.LabelCentered({ body.x, body.y + 30.0f, body.w, 22.0f },
                             "Збережень ще немає", m_theme.textDim);
        }
        else
        {
            const f32 rowHeight = 40.0f;
            const Rect content = m_ui.BeginScroll(body, m_saves.size() * rowHeight, m_saveScroll);
            for (size_t i = 0; i < m_saves.size(); ++i)
            {
                const Rect row{ content.x, content.y + i * rowHeight, content.w, rowHeight - 4.0f };
                if (m_ui.ListItem(row, m_saves[i].name, static_cast<i32>(i) == m_selectedSave))
                {
                    m_selectedSave = static_cast<i32>(i);
                    m_saveName = m_saves[i].name;   // clicking a slot fills the name field
                }
                m_ui.LabelRight({ row.x, row.y, row.w - m_theme.padding, row.h },
                              m_saves[i].dateText, m_theme.textDim);
            }
            m_ui.EndScroll();
        }

        const f32 fieldY = panel.Bottom() - 84.0f;
        m_ui.Label({ panel.x + m_theme.padding, fieldY, 110.0f, 26.0f }, "Назва", m_theme.textDim);
        m_ui.TextField({ panel.x + m_theme.padding + 90.0f, fieldY,
                       panel.w - m_theme.padding * 2.0f - 90.0f, 26.0f }, "saveName", m_saveName, 40);

        const f32 buttonY = panel.Bottom() - 44.0f;
        const f32 buttonWidth = (panel.w - m_theme.padding * 3.0f) * 0.5f;

        if (m_ui.Button({ panel.x + m_theme.padding, buttonY, buttonWidth, 32.0f }, "Скасувати"))
        {
            m_saveDialogOpen = false;
        }
        if (m_ui.Button({ panel.x + m_theme.padding * 2.0f + buttonWidth, buttonY, buttonWidth, 32.0f },
                      "Зберегти", !m_saveName.empty()))
        {
            PerformSave(m_saveName);
            m_saves = SaveGame::List();
            m_saveDialogOpen = false;
            m_pauseMenuOpen = false;
        }
    }

    void GameScene::DrawWorld()
    {
        DrawMines();
        DrawSettlements();
        DrawCohorts();
        DrawSelectionMarkers();
        DrawOrderPreview();
        DrawRememberedSettlements();
        DrawCohortLabels();
        DrawCohortBars();
        DrawSettlementLabels();
        DrawDiplomaticFlares(m_renderer.DeltaTime());
    }

    void GameScene::DrawDiplomaticFlares(f32 deltaTime)
    {
        std::vector<DiplomaticFlare>& flares = m_world.Flares();
        if (flares.empty()) return;

        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();

        for (DiplomaticFlare& flare : flares)
        {
            flare.life -= deltaTime;
            if (flare.life <= 0.0f) continue;

            const f32 age = 1.0f - flare.life / std::max(0.01f, flare.duration);

            // The line is struck out quickly and then fades: the eye catches the movement,
            // and a second later the map is clean again.
            const f32 reach = std::min(1.0f, age * 3.5f);
            const f32 alpha = age < 0.35f ? 1.0f : 1.0f - (age - 0.35f) / 0.65f;

            const Vec2 from = camera.MapToScreen(flare.from, map.WorldHeightAtMap(flare.from));
            const Vec2 target = camera.MapToScreen(flare.to, map.WorldHeightAtMap(flare.to));
            const Vec2 head{ from.x + (target.x - from.x) * reach,
                             from.y + (target.y - from.y) * reach };

            m_renderer.UILine(from, head, flare.color.WithAlpha(alpha * 0.45f), 6.0f);
            m_renderer.UILine(from, head, flare.color.WithAlpha(alpha), 2.0f);

            // A mark at each end, so it is clear which two realms this concerns.
            const f32 pulse = 5.0f + 3.0f * std::sin(m_pulse * 12.0f);
            m_renderer.UIRect({ from.x - pulse * 0.5f, from.y - pulse * 0.5f, pulse, pulse },
                              flare.color.WithAlpha(alpha));
            if (reach >= 1.0f)
            {
                m_renderer.UIRect({ target.x - pulse * 0.5f, target.y - pulse * 0.5f, pulse, pulse },
                                  flare.color.WithAlpha(alpha));
            }
        }

        flares.erase(std::remove_if(flares.begin(), flares.end(),
                                    [](const DiplomaticFlare& flare) { return flare.life <= 0.0f; }),
                     flares.end());
    }

    void GameScene::DrawSettlements()
    {
        const MapData& map = m_world.Map();
        const Camera& camera = m_renderer.GetCamera();

        const f32 size = ConfigManager::Get().Float("render/spriteScale/settlement", 26.0f);
        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 160.0f };
        const Vec2 focus = camera.Focus();

        FogSystem& fog = FogSystem::Get();

        // Under fog a town is drawn live only while somebody of yours is watching it. Where
        // nobody is, the memory of it is drawn instead - see DrawRememberedSettlements.
        for (const auto& [id, settlement] : m_world.Settlements())
        {
            if (std::abs(settlement.position.x - focus.x) > half.x) continue;
            if (std::abs(settlement.position.y - focus.y) > half.y) continue;
            if (!fog.IsVisible(m_world, settlement.position)) continue;

            const Color tint = settlement.IsIndependent()
                ? Color::FromRGB(0xB9C0C8)
                : ClanColor(settlement.owner);

            const f32 height = map.WorldHeightAtMap(settlement.position);
            // A town that has outgrown its tier draws a bigger picture as well as a
            // different one, so growth reads at a glance even at low zoom.
            const f32 growth = 1.0f + ConfigManager::Get().Float("render/spriteScale/tierGrowth", 0.14f) *
                                      static_cast<f32>(settlement.TierIndex());
            const f32 scale = growth * (settlement.kind == SettlementKind::Village
                ? size * 0.8f
                : size * (settlement.kind == SettlementKind::City ? 1.15f : 1.0f));

            // A besieged town pulses so the player cannot miss it.
            const f32 flash = settlement.besiegedBy != kInvalidId
                ? 0.35f + 0.35f * std::sin(m_pulse * 6.0f)
                : 0.0f;

            // Centred on the map position: the icon, the selection ring and the click
            // target then all sit on exactly the same point. The negative depth bias keeps
            // the town in front of any army standing on it, which matters during a siege.
            SpriteInstance instance;
            instance.worldPosition = Camera::ToWorld(settlement.position, height);
            instance.size = { scale, scale };
            instance.uvRect = m_renderer.SpriteUV(settlement.Sprite());
            instance.color = tint;
            instance.params = { 0.5f, -1.0f, flash, 0.0f };
            m_renderer.DrawSpriteRaw(instance);

            // A town with troops in it wears their race as a small badge on its lower right,
            // so you can read a garrison off the map without opening anything.
            EntityId garrison = kInvalidId;
            for (const auto& [cohortId, cohort] : m_world.Cohorts())
            {
                if (cohort.garrisonOf == id && !cohort.IsEmpty()) { garrison = cohortId; break; }
            }
            if (garrison == kInvalidId) continue;

            const Cohort* troops = m_world.FindCohort(garrison);
            const Clan* holder = troops ? m_world.FindClan(troops->clan) : nullptr;
            if (!holder) continue;

            const f32 badge = scale * ConfigManager::Get().Float("render/garrisonBadgeScale", 0.42f);

            SpriteInstance marker;
            marker.worldPosition = Camera::ToWorld(
                { settlement.position.x + scale * 0.34f, settlement.position.y + scale * 0.30f },
                height);
            marker.size = { badge, badge };
            marker.uvRect = m_renderer.SpriteUV(RaceDatabase::Get().Race(holder->raceId).sprite);
            marker.color = Color(1.0f, 1.0f, 1.0f, 1.0f);
            marker.params = { 0.5f, -2.0f, 0.0f, 0.0f };   // in front of the town itself
            m_renderer.DrawSpriteRaw(marker);
        }
    }

    void GameScene::DrawRememberedSettlements()
    {
        FogSystem& fog = FogSystem::Get();
        if (!fog.IsEnabled()) return;

        const MapData& map = m_world.Map();
        const Camera& camera = m_renderer.GetCamera();
        const f32 size = ConfigManager::Get().Float("render/spriteScale/settlement", 26.0f);
        const f32 dim = ConfigManager::Get().Float("render/fog/memoryAlpha", 0.55f);

        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 160.0f };
        const Vec2 focus = camera.Focus();

        for (const FogSystem::SeenSettlement& seen : fog.Remembered())
        {
            // A town under watch is drawn live by DrawSettlements; this is only for the
            // ones the player is going on memory for.
            if (fog.IsVisible(m_world, seen.position)) continue;
            if (std::abs(seen.position.x - focus.x) > half.x) continue;
            if (std::abs(seen.position.y - focus.y) > half.y) continue;

            const f32 scale = seen.kind == SettlementKind::Village
                ? size * 0.8f
                : size * (seen.kind == SettlementKind::City ? 1.15f : 1.0f);

            SpriteInstance instance;
            instance.worldPosition = Camera::ToWorld(seen.position, map.WorldHeightAtMap(seen.position));
            instance.size = { scale, scale };
            instance.uvRect = m_renderer.SpriteUV(seen.sprite);
            instance.color = seen.color.WithAlpha(dim);
            instance.params = { 0.5f, -1.0f, 0.0f, 0.0f };
            m_renderer.DrawSpriteRaw(instance);
        }
    }

    void GameScene::DrawCohorts()
    {
        const MapData& map = m_world.Map();
        const Camera& camera = m_renderer.GetCamera();

        const f32 size = ConfigManager::Get().Float("render/spriteScale/cohort", 20.0f);
        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 160.0f };
        const Vec2 focus = camera.Focus();

        for (const auto& [id, cohort] : m_world.Cohorts())
        {
            if (std::abs(cohort.position.x - focus.x) > half.x) continue;
            if (std::abs(cohort.position.y - focus.y) > half.y) continue;

            // A garrison is inside its walls: it is commanded from the settlement panel,
            // and drawing it on the map would only clutter the town it is sitting in.
            if (cohort.garrisonOf != kInvalidId) continue;

            // An army is only ever where you can see it. You remember where a town stood;
            // you do not remember where a column happened to be a fortnight ago.
            if (!FogSystem::Get().IsVisible(m_world, cohort.position)) continue;

            // A host in contact shakes where it stands. Two sine waves at frequencies that
            // do not divide into each other, offset by the army's own id, so neighbouring
            // banners never tremble in step - it reads as a scuffle, not as a pulse.
            Vec2 drawPosition = cohort.position;
            if (cohort.inBattle)
            {
                const f32 amplitude = ConfigManager::Get().Float("render/battleShake", 1.6f);
                const f32 phase = static_cast<f32>(id % 97) * 0.37f;
                drawPosition.x += std::sin(m_pulse * 27.0f + phase) * amplitude;
                drawPosition.y += std::sin(m_pulse * 41.0f + phase * 1.7f) * amplitude * 0.8f;
            }

            const f32 height = map.WorldHeightAtMap(drawPosition);
            const f32 flash = cohort.inBattle ? 0.4f + 0.4f * std::sin(m_pulse * 10.0f) : 0.0f;
            m_renderer.DrawSprite(SpriteId::Cohort, drawPosition, height, size,
                                ClanColor(cohort.clan), 0.5f, flash);

            // Who is marching sits in the upper half of the banner; how many of them is
            // written underneath it by DrawCohortLabels, inside the same square.
            const Clan* clan = m_world.FindClan(cohort.clan);
            if (clan)
            {
                const f32 markerSize = size * ConfigManager::Get().Float("render/cohortRaceScale", 0.5f);
                const SpriteId raceSprite = RaceDatabase::Get().Race(clan->raceId).sprite;

                SpriteInstance marker;
                marker.worldPosition = Camera::ToWorld(drawPosition, height);
                marker.size = { markerSize, markerSize };
                marker.uvRect = m_renderer.SpriteUV(raceSprite);
                // The race icons carry their own colours in the sheet. Tinting them here
                // would throw that away, so they go up untouched.
                marker.color = Color(1.0f, 1.0f, 1.0f, 1.0f);

                // The anchor is a vertical offset in units of the sprite's own height, and
                // it is applied in view space - so this raises the icon on screen at any
                // camera angle. Below 0.5 is upwards.
                const f32 lift = ConfigManager::Get().Float("render/cohortRaceLift", 0.22f);
                marker.params = { 0.5f - lift, -1.0f, flash, 0.0f };
                m_renderer.DrawSpriteRaw(marker);
            }
        }
    }

    void GameScene::DrawCohortLabels()
    {
        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();
        ConfigManager& config = ConfigManager::Get();

        const f32 size = config.Float("render/spriteScale/cohort", 20.0f);
        const f32 minZoom = config.Float("render/cohortCountMinZoom", 0.7f);
        if (camera.Zoom() < minZoom) return;

        // The text is sized to the banner, so it stays inside the square at every zoom.
        const f32 square = size * camera.Zoom();
        const f32 scale = std::clamp(square / 34.0f, 0.55f, 1.5f);

        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 160.0f };
        const Vec2 focus = camera.Focus();

        for (const auto& [id, cohort] : m_world.Cohorts())
        {
            if (cohort.garrisonOf != kInvalidId) continue;
            if (!FogSystem::Get().IsVisible(m_world, cohort.position)) continue;
            if (std::abs(cohort.position.x - focus.x) > half.x) continue;
            if (std::abs(cohort.position.y - focus.y) > half.y) continue;

            const std::string text = ShortCount(m_world.CohortStrength(id));
            const Vec2 screen = camera.MapToScreen(cohort.position,
                                                   map.WorldHeightAtMap(cohort.position));

            const f32 lineHeight = m_renderer.TextHeight(scale);
            const Rect box{ screen.x - square * 0.5f,
                            screen.y + square * config.Float("render/cohortCountDrop", 0.1f),
                            square, lineHeight };

            // A shadow under the digits: a banner can be any colour, and white on yellow
            // needs the help.
            m_renderer.UITextCentered(text, { box.x + 1.0f, box.y + 1.0f, box.w, box.h },
                                      m_theme.shadow.WithAlpha(0.8f), scale);
            m_renderer.UITextCentered(text, box, Color(1.0f, 1.0f, 1.0f, 1.0f), scale);
        }
    }

    void GameScene::DrawCohortBars()
    {
        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();
        ConfigManager& config = ConfigManager::Get();

        // Below a certain zoom the bars would be a smear of pixels; leave the map clean.
        const f32 minZoom = config.Float("render/cohortBarMinZoom", 0.8f);
        if (camera.Zoom() < minZoom) return;

        const f32 size = config.Float("render/spriteScale/cohort", 20.0f);
        const f32 width = size * camera.Zoom() * 0.9f;
        const f32 height = std::max(2.0f, 2.5f * camera.Zoom() * 0.5f);

        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 160.0f };
        const Vec2 focus = camera.Focus();

        for (const auto& [id, cohort] : m_world.Cohorts())
        {
            if (cohort.garrisonOf != kInvalidId) continue;
            if (!FogSystem::Get().IsVisible(m_world, cohort.position)) continue;
            if (std::abs(cohort.position.x - focus.x) > half.x) continue;
            if (std::abs(cohort.position.y - focus.y) > half.y) continue;

            const Vec2 screen = camera.MapToScreen(cohort.position,
                                                   map.WorldHeightAtMap(cohort.position));
            f32 y = screen.y + size * camera.Zoom() * 0.5f + 3.0f;

            // Supply first, then order: how well fed, then how well held together.
            const std::pair<f32, Color> bars[2] = {
                { Clamp01(cohort.supply), m_theme.positive },
                { Clamp01(cohort.organisation), m_theme.accent },
            };
            for (const auto& [value, tint] : bars)
            {
                const Rect track{ screen.x - width * 0.5f, y, width, height };
                m_renderer.UIRect(track, m_theme.shadow.WithAlpha(0.7f));
                m_renderer.UIRect({ track.x, track.y, track.w * value, track.h },
                                  value > 0.3f ? tint : m_theme.negative);
                y += height + 1.0f;
            }
        }
    }

    void GameScene::DrawMines()
    {
        const MapData& map = m_world.Map();
        const f32 size = ConfigManager::Get().Float("render/spriteScale/quarry", 18.0f);

        for (const MineSite& mine : m_world.Mines())
        {
            if (!FogSystem::Get().IsKnown(m_world, mine.position)) continue;
            const Color tint = mine.developed && mine.owner != kInvalidId
                ? ClanColor(mine.owner)
                : Color::FromRGB(0x9AA3AB);
            m_renderer.DrawSprite(SpriteId::Quarry, mine.position, map.WorldHeightAtMap(mine.position),
                                size, tint, 0.5f);
        }
    }

    void GameScene::DrawSettlementLabels()
    {
        FogSystem& fog = FogSystem::Get();
        const Camera& camera = m_renderer.GetCamera();
        const MapData& map = m_world.Map();
        ConfigManager& config = ConfigManager::Get();

        const Settings& settings = Settings::Get();

        const f32 minZoom = settings.labelMinZoom;
        const f32 scale = config.Float("render/labels/scale", 0.95f);
        const f32 offset = config.Float("render/labels/offset", 6.0f);
        const f32 settlementSize = config.Float("render/spriteScale/settlement", 26.0f);
        const bool showNames = settings.showLabels && camera.Zoom() >= minZoom;

        const Vec2 viewport = m_renderer.ViewportSize();
        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 120.0f };
        const Vec2 focus = camera.Focus();

        for (const auto& [id, settlement] : m_world.Settlements())
        {
            if (std::abs(settlement.position.x - focus.x) > half.x) continue;
            if (std::abs(settlement.position.y - focus.y) > half.y) continue;

            // A name is written where the town is drawn. In remembered country that is the
            // name as it was last heard, which for a town is the same thing.
            if (!fog.IsKnown(m_world, settlement.position)) continue;

            const bool besieged = settlement.besiegedBy != kInvalidId;
            if (!showNames && !besieged) continue;   // a siege is always worth announcing

            const Vec2 screen = camera.MapToScreen(settlement.position,
                                                   map.WorldHeightAtMap(settlement.position));
            if (screen.x < -80.0f || screen.x > viewport.x + 80.0f) continue;
            if (screen.y < -40.0f || screen.y > viewport.y + 40.0f) continue;

            // The icon is centred on its map point, so half of it stands above the point.
            const f32 iconHalf = settlementSize * 0.5f * camera.Zoom();

            // A siege is an alert: it hangs above the town where nothing else competes with it.
            if (besieged)
            {
                const Clan* besieger = m_world.FindClan(settlement.besiegedBy);
                const std::string text = "ОБЛОГА " + Percent(settlement.siegeProgress);
                const f32 width = m_renderer.TextWidth(text, scale);
                const Rect box{ screen.x - width * 0.5f - 5.0f,
                                screen.y - iconHalf - offset - m_renderer.TextHeight(scale),
                                width + 10.0f, m_renderer.TextHeight(scale) + 6.0f };

                m_renderer.UIRect(box, m_theme.shadow.WithAlpha(0.80f));
                m_renderer.UIRect({ box.x, box.Bottom() - 2.0f, box.w * Clamp01(settlement.siegeProgress), 2.0f },
                                besieger ? besieger->color : m_theme.negative);
                m_renderer.UITextCentered(text, box, m_theme.negative, scale);
            }

            if (!showNames) continue;

            // The name sits under the icon, so the plate never covers the settlement itself.
            const Clan* owner = m_world.FindClan(settlement.owner);
            const Color color = LabelColor(owner ? owner->color : Color::FromRGB(0xC9D2DA));
            const f32 width = m_renderer.TextWidth(settlement.name, scale);
            const Rect plate{ screen.x - width * 0.5f - 4.0f, screen.y + iconHalf + 2.0f,
                              width + 8.0f, m_renderer.TextHeight(scale) + 4.0f };

            m_renderer.UIRect(plate, m_theme.shadow.WithAlpha(0.72f));
            m_renderer.UITextCentered(settlement.name, plate, color, scale);
        }
    }

    void GameScene::DrawSelectionMarkers()
    {
        const MapData& map = m_world.Map();

        auto ring = [&](const Vec2& position, f32 size, const Color& color, f32 alpha)
        {
            m_renderer.DrawSprite(SpriteId::Circle, position, map.WorldHeightAtMap(position),
                                size, color.WithAlpha(alpha), 0.5f);
        };

        // While a site is being chosen the cursor carries a breathing ring: it says both
        // "here" and "you are still deciding", without a line of text.
        if (m_placingSettlement && !m_ui.WantsMouse())
        {
            const Vec2 site = ScreenToTerrain(m_input.MousePosition());
            const Clan* founder = m_world.HumanClan();
            const bool allowed = SettlementFactory::CanPlace(m_world, m_world.Map(), m_placingKind, site,
                                                             founder ? founder->id : kInvalidId);

            // A full triangle wave from nothing to solid and back, so the pulse is even.
            const f32 phase = std::fmod(m_pulse * ConfigManager::Get().Float("render/placePulseSpeed", 1.1f), 1.0f);
            const f32 alpha = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;

            const f32 size = ConfigManager::Get().Float("render/placeRingSize", 56.0f);
            ring(site, size, allowed ? m_theme.positive : m_theme.negative, alpha);
        }

        if (m_hoveredSettlement != kInvalidId)
        {
            if (const Settlement* settlement = m_world.FindSettlement(m_hoveredSettlement))
            {
                ring(settlement->position, 34.0f, m_theme.textStrong, 0.28f);
            }
        }
        if (m_hoveredCohort != kInvalidId)
        {
            if (const Cohort* cohort = m_world.FindCohort(m_hoveredCohort))
            {
                ring(cohort->position, 28.0f, m_theme.textStrong, 0.28f);
            }
        }
        if (m_hoveredMine != kInvalidId)
        {
            if (const MineSite* mine = m_world.FindMine(m_hoveredMine))
            {
                ring(mine->position, 28.0f, m_theme.textStrong, 0.28f);
            }
        }
        if (m_selectionKind == SelectionKind::Mine)
        {
            if (const MineSite* mine = m_world.FindMine(m_selected))
            {
                ring(mine->position, 32.0f, m_theme.selection, 0.45f + 0.2f * std::sin(m_pulse * 4.0f));
            }
        }

        const f32 pulse = 0.45f + 0.2f * std::sin(m_pulse * 4.0f);
        if (m_selectionKind == SelectionKind::Settlement)
        {
            if (const Settlement* settlement = m_world.FindSettlement(m_selected))
            {
                ring(settlement->position, 40.0f, m_theme.selection, pulse);
            }
        }
        else if (m_selectionKind == SelectionKind::Cohort)
        {
            // The whole band is ringed; the one whose panel is open rings a little brighter.
            for (EntityId id : m_selectedCohorts)
            {
                const Cohort* cohort = m_world.FindCohort(id);
                if (!cohort) continue;
                const f32 alpha = id == m_selected ? pulse : pulse * 0.6f;
                ring(cohort->position, id == m_selected ? 32.0f : 28.0f, m_theme.selection, alpha);
            }
        }
    }

    void GameScene::DrawOrderPreview()
    {
        if (m_selectionKind != SelectionKind::Cohort) return;

        const Camera& camera = m_renderer.GetCamera();

        const Cohort* cohort = m_world.FindCohort(m_selected);
        if (!cohort) return;

        // Draw the planned march as a screen-space polyline so it stays visible at any zoom.
        const Task& task = cohort->currentTask;
        if (task.waypointIndex < task.waypoints.size())
        {
            Vec2 previous = camera.MapToScreen(cohort->position, m_world.Map().WorldHeightAtMap(cohort->position));
            for (size_t i = task.waypointIndex; i < task.waypoints.size(); ++i)
            {
                const Vec2 point = camera.MapToScreen(task.waypoints[i],
                                                      m_world.Map().WorldHeightAtMap(task.waypoints[i]));
                m_renderer.UILine(previous, point, m_theme.selection.WithAlpha(0.75f), 2.0f);
                previous = point;
            }
            m_renderer.UIRect({ previous.x - 4.0f, previous.y - 4.0f, 8.0f, 8.0f }, m_theme.selection);
        }
    }
}
