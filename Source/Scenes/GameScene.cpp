#include "GameScene.h"
#include "SceneManager.h"

#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Game/Systems/BattleSystem.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/Systems/MovementSystem.h"
#include "../Game/Systems/PoliticsSystem.h"
#include "../Game/Systems/SettlementSystem.h"
#include "../Game/Systems/Simulation.h"
#include "../Game/World/RaceDatabase.h"
#include "../Game/World/World.h"
#include "../Game/WorldGenerator.h"
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

        Renderer::Get().SetTerrainEnabled(true);
        Renderer::Get().SetClearColor(Color::FromRGB(0x00fbf2));   // open sea beyond the map, matching the lit water

        World& world = World::Get();
        const std::string saveFile = scenes.Payload("load");
        bool ready = false;

        if (!saveFile.empty())
        {
            ready = SaveGame::Load(world, saveFile, m_mapFolder);
            scenes.SetPayload("load", "");   // a later restart must not reload the same file
            if (ready)
            {
                WorldGenerator::PublishMapToRenderer(world);
                m_saveName = world.HumanState() ? world.HumanState()->name : std::string("Гра");
            }
        }
        else
        {
            const PartySettings settings = PartySettings::FromJson(scenes.Data("party"));
            m_mapFolder = settings.mapFolder;
            ready = WorldGenerator::Generate(world, settings);
            if (ready) m_saveName = world.HumanState() ? world.HumanState()->name : std::string("Гра");
        }

        if (!ready)
        {
            WOC_LOG_ERROR("Could not start the game; returning to the menu");
            scenes.Request(SceneId::MainMenu);
            return;
        }

        // Open on the player's own seat, zoomed in close enough to read it.
        if (Clan* clan = world.HumanClan())
        {
            const Settlement* seat = nullptr;
            for (EntityId id : clan->settlements)
            {
                const Settlement* candidate = world.FindSettlement(id);
                if (!candidate) continue;
                if (!seat || candidate->kind == SettlementKind::City) seat = candidate;
                if (seat && seat->kind == SettlementKind::City) break;
            }
            if (seat)
            {
                Camera& camera = Renderer::Get().GetCamera();
                camera.SetZoom(ConfigManager::Get().Float("camera/startZoom", 2.2f));
                camera.SetFocus(seat->position);
                camera.ClampToBounds();
                m_selectionKind = SelectionKind::Settlement;
                m_selected = seat->id;
            }
        }
    }

    void GameScene::OnExit()
    {
        World::Get().Reset();
    }

    Color GameScene::ClanColor(EntityId clanId) const
    {
        const Clan* clan = World::Get().FindClan(clanId);
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

        UpdateHotkeys();
        // The world stands still behind a modal, and once the party is decided.
        if (m_pauseMenuOpen || m_saveDialogOpen || m_gameOver) return;

        UpdateCamera(deltaTime);
        UpdateSelection();

        Simulation::Get().Update(World::Get(), deltaTime);
    }

    void GameScene::UpdateCamera(f32 deltaTime)
    {
        ConfigManager& config = ConfigManager::Get();
        Camera& camera = Renderer::Get().GetCamera();
        Input& input = Input::Get();
        UI& ui = UI::Get();

        const Vec2 viewport = Renderer::Get().ViewportSize();
        camera.SetViewport(viewport.x, viewport.y);

        if (ui.WantsKeyboard()) return;

        // Q and E spin the map, R puts it back the way it started.
        const f32 rotateSpeed = Settings::Get().rotateSpeed;
        if (input.IsKeyDown(Key::Q)) camera.RotateBy(-rotateSpeed * deltaTime);
        if (input.IsKeyDown(Key::E)) camera.RotateBy(rotateSpeed * deltaTime);
        if (input.WasKeyPressed(Key::R)) camera.ResetRotation();

        const f32 panSpeed = config.Float("camera/panSpeed", 900.0f) / camera.Zoom();
        Vec2 pan;
        if (input.IsKeyDown(Key::A) || input.IsKeyDown(Key::Left)) pan.x -= 1.0f;
        if (input.IsKeyDown(Key::D) || input.IsKeyDown(Key::Right)) pan.x += 1.0f;
        if (input.IsKeyDown(Key::W) || input.IsKeyDown(Key::Up)) pan.y -= 1.0f;
        if (input.IsKeyDown(Key::S) || input.IsKeyDown(Key::Down)) pan.y += 1.0f;

        // Edge scrolling, unless the pointer is over the interface.
        const f32 margin = Settings::Get().edgeScroll;
        const Vec2 mouse = input.MousePosition();
        if (!ui.WantsMouse() && margin > 0.0f)
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

        const f32 wheel = input.WheelDelta();
        if (wheel != 0.0f && !ui.WantsMouse())
        {
            const f32 step = config.Float("camera/zoomStep", 1.12f);
            camera.ZoomAt(wheel > 0.0f ? step : 1.0f / step, mouse);
        }
    }

    void GameScene::UpdateHotkeys()
    {
        Input& input = Input::Get();
        UI& ui = UI::Get();
        if (ui.WantsKeyboard()) return;

        Simulation& simulation = Simulation::Get();

        if (input.WasKeyPressed(Key::Space)) simulation.TogglePause();
        if (input.WasKeyPressed(Key::Plus)) simulation.SetSpeedIndex(simulation.SpeedIndex() + 1);
        if (input.WasKeyPressed(Key::Minus)) simulation.SetSpeedIndex(simulation.SpeedIndex() - 1);
        // 0 pauses, 1-5 pick a speed step.
        for (i32 i = 0; i <= 5; ++i)
        {
            if (input.WasKeyPressed(static_cast<Key>(static_cast<u16>(Key::Num0) + i)))
            {
                simulation.SetSpeedIndex(i);
            }
        }

        // Snapshot the living world back into the map folder, so a party you like becomes
        // the authored starting position for that map.
        if (input.WasKeyPressed(Key::F9))
        {
            m_status = WorldGenerator::SaveObjects(World::Get(), m_mapFolder)
                ? "MapObjects.json збережено для карти " + m_mapFolder
                : "Не вдалося зберегти MapObjects.json";
            m_statusTimer = 4.0f;
        }

        if (input.WasKeyPressed(Key::F1))
        {
            Renderer& renderer = Renderer::Get();
            renderer.SetBordersVisible(!renderer.BordersVisible());
            m_status = renderer.BordersVisible() ? "Кордони: увімкнено" : "Кордони: вимкнено";
            m_statusTimer = 2.5f;
        }
        if (input.WasKeyPressed(Key::F2)) m_panelMode = PanelMode::Realm;
        if (input.WasKeyPressed(Key::F3)) m_panelMode = PanelMode::Diplomacy;
        if (input.WasKeyPressed(Key::F4)) m_panelMode = PanelMode::Chronicle;

        if (input.WasKeyPressed(Key::Escape))
        {
            // Escape backs out one step at a time and only ever opens the menu; leaving a
            // running game is a deliberate choice made in that menu, never a stray keypress.
            if (m_saveDialogOpen) m_saveDialogOpen = false;
            else if (m_pauseMenuOpen) m_pauseMenuOpen = false;
            else if (m_placingSettlement) m_placingSettlement = false;
            else if (m_selectedCharacter != kInvalidId) m_selectedCharacter = kInvalidId;
            else if (m_selectedUnit != kInvalidId) m_selectedUnit = kInvalidId;
            else if (m_selectionKind != SelectionKind::None)
            {
                m_selectionKind = SelectionKind::None;
                m_selected = kInvalidId;
            }
            else m_pauseMenuOpen = true;
        }
    }

    Vec2 GameScene::ScreenToTerrain(const Vec2& screenPoint) const
    {
        const Camera& camera = Renderer::Get().GetCamera();
        const MapData& map = World::Get().Map();

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
        const World& world = World::Get();
        const Camera& camera = Renderer::Get().GetCamera();
        const MapData& map = world.Map();

        EntityId best = kInvalidId;
        f32 bestDistance = screenRadius * screenRadius;

        for (const auto& [id, settlement] : world.Settlements())
        {
            const Vec2 screen = camera.MapToScreen(settlement.position,
                                                   map.WorldHeightAtMap(settlement.position));
            const f32 distance = DistanceSq(screen, screenPoint);
            if (distance < bestDistance) { bestDistance = distance; best = id; }
        }
        return best;
    }

    EntityId GameScene::PickCohort(const Vec2& screenPoint, f32 screenRadius) const
    {
        const World& world = World::Get();
        const Camera& camera = Renderer::Get().GetCamera();
        const MapData& map = world.Map();

        EntityId best = kInvalidId;
        f32 bestDistance = screenRadius * screenRadius;

        for (const auto& [id, cohort] : world.Cohorts())
        {
            // Garrisons are commanded from the settlement panel, not from the map.
            if (cohort.garrisonOf != kInvalidId) continue;

            const Vec2 screen = camera.MapToScreen(cohort.position,
                                                   map.WorldHeightAtMap(cohort.position));
            const f32 distance = DistanceSq(screen, screenPoint);
            if (distance < bestDistance) { bestDistance = distance; best = id; }
        }
        return best;
    }

    void GameScene::UpdateSelection()
    {
        Input& input = Input::Get();
        UI& ui = UI::Get();
        World& world = World::Get();
        Camera& camera = Renderer::Get().GetCamera();

        const Vec2 screen = input.MousePosition();
        const Vec2 mapPosition = ScreenToTerrain(screen);
        const f32 pickRadius = ConfigManager::Get().Float("render/pickRadius", 20.0f);

        m_hoveredCohort = ui.WantsMouse() ? kInvalidId : PickCohort(screen, pickRadius);
        m_hoveredSettlement = (ui.WantsMouse() || m_hoveredCohort != kInvalidId)
            ? kInvalidId : PickSettlement(screen, pickRadius);

        if (ui.WantsMouse()) return;

        if (input.WasMousePressed(MouseButton::Left))
        {
            if (m_placingSettlement)
            {
                Clan* clan = world.HumanClan();
                if (clan)
                {
                    const EntityId created = SettlementSystem::Get().Found(world, clan->id,
                                                                           m_placingKind, mapPosition);
                    if (created != kInvalidId)
                    {
                        m_selectionKind = SelectionKind::Settlement;
                        m_selected = created;
                        m_status = "Поселення засновано";
                    }
                    else
                    {
                        m_status = "Тут будувати не можна";
                    }
                    m_statusTimer = 3.0f;
                }
                m_placingSettlement = false;
                return;
            }

            if (m_hoveredCohort != kInvalidId)
            {
                m_selectionKind = SelectionKind::Cohort;
                m_selected = m_hoveredCohort;
                m_selectedUnit = kInvalidId;
                m_selectedCharacter = kInvalidId;
                m_panelMode = PanelMode::Selection;
            }
            else if (m_hoveredSettlement != kInvalidId)
            {
                m_selectionKind = SelectionKind::Settlement;
                m_selected = m_hoveredSettlement;
                m_panelMode = PanelMode::Selection;
            }
            else
            {
                m_selectionKind = SelectionKind::None;
                m_selected = kInvalidId;
            }
        }

        if (input.WasMousePressed(MouseButton::Right))
        {
            IssueOrder(mapPosition);
        }
    }

    void GameScene::IssueOrder(const Vec2& mapPosition)
    {
        if (m_selectionKind != SelectionKind::Cohort) return;

        World& world = World::Get();
        Cohort* cohort = world.FindCohort(m_selected);
        if (!cohort) return;

        // Orders may only be given to one's own armies.
        const State* humanState = world.HumanState();
        const Clan* clan = world.FindClan(cohort->clan);
        if (!humanState || !clan || clan->state != humanState->id) return;

        MovementSystem& movement = MovementSystem::Get();

        const Vec2 screen = Input::Get().MousePosition();
        const EntityId targetSettlement = PickSettlement(screen, 26.0f);
        if (targetSettlement != kInvalidId)
        {
            Settlement* settlement = world.FindSettlement(targetSettlement);
            if (settlement)
            {
                const bool own = settlement->owner == cohort->clan;
                const bool hostile = world.AreHostile(cohort->clan, settlement->owner) ||
                                     settlement->IsIndependent();

                TaskType task = TaskType::Move;
                if (own) task = TaskType::Garrison;
                else if (hostile) task = Input::Get().IsKeyDown(Key::Shift) ? TaskType::Raid : TaskType::Besiege;

                if (movement.OrderTask(world, cohort->id, task, settlement->position, targetSettlement))
                {
                    m_status = std::string(Task::TypeName(task)) + ": " + settlement->name;
                    m_statusTimer = 3.0f;
                }
                else
                {
                    m_status = "Туди не пройти";
                    m_statusTimer = 3.0f;
                }
                return;
            }
        }

        const EntityId targetCohort = PickCohort(screen, 22.0f);
        if (targetCohort != kInvalidId && targetCohort != cohort->id)
        {
            const Cohort* enemy = world.FindCohort(targetCohort);
            if (enemy && world.AreHostile(cohort->clan, enemy->clan))
            {
                movement.OrderTask(world, cohort->id, TaskType::Attack, enemy->position,
                                   kInvalidId, targetCohort);
                m_status = "Перехоплення: " + enemy->DisplayName();
                m_statusTimer = 3.0f;
                return;
            }
        }

        if (!movement.OrderMove(world, cohort->id, mapPosition))
        {
            m_status = "Туди не пройти";
            m_statusTimer = 3.0f;
        }
    }

    // =====================================================================================
    // Rendering
    // =====================================================================================

    void GameScene::Render()
    {
        DrawWorld();

        DrawTopBar();
        DrawSidePanel();
        DrawBottomBar();
        DrawChronicle();
        DrawTooltipForHover();
        DrawPauseMenu();
        DrawSaveDialog();
        DrawGameOver();
    }

    void GameScene::DrawGameOver()
    {
        if (!m_gameOver) return;

        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();

        const f32 fade = ui.Transition("game.over", true, 0.6f);
        const Vec2 viewport = renderer.ViewportSize();
        renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, theme.shadow.WithAlpha(0.78f * fade));

        const f32 width = 560.0f;
        const f32 height = 280.0f;
        const Rect panel{ (viewport.x - width) * 0.5f,
                          (viewport.y - height) * 0.5f + (1.0f - fade) * 40.0f, width, height };
        ui.Panel(panel);
        ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const Color accent = m_victory ? theme.accent : theme.negative;
        renderer.UITextCentered(m_victory ? "ПЕРЕМОГА" : "ПОРАЗКА",
                                { panel.x, panel.y + 34.0f, panel.w, 44.0f }, accent, 2.2f);

        const State* state = World::Get().HumanState();
        if (state)
        {
            renderer.UITextCentered(state->name, { panel.x, panel.y + 86.0f, panel.w, 24.0f },
                                    theme.textStrong);
        }

        ui.Paragraph({ panel.x + 40.0f, panel.y + 122.0f, panel.w - 80.0f, 0.0f },
                     PoliticsSystem::Get().Reason(), theme.text);

        renderer.UITextCentered(World::Get().Time().ToString(),
                                { panel.x, panel.y + 174.0f, panel.w, 20.0f }, theme.textDim);

        const f32 buttonWidth = 220.0f;
        const f32 y = panel.Bottom() - 56.0f;
        if (ui.Button({ panel.Center().x - buttonWidth - 6.0f, y, buttonWidth, 34.0f }, "У головне меню"))
        {
            SceneManager::Get().Request(SceneId::MainMenu);
        }
        if (ui.Button({ panel.Center().x + 6.0f, y, buttonWidth, 34.0f }, "Вихід із гри"))
        {
            SceneManager::Get().RequestQuit();
        }
    }

    void GameScene::DrawPauseMenu()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();

        const f32 fade = ui.Transition("game.pause", m_pauseMenuOpen, 0.15f);
        if (fade <= 0.001f) return;

        const Vec2 viewport = renderer.ViewportSize();
        renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, theme.shadow.WithAlpha(0.6f * fade));

        const f32 width = 340.0f;
        const f32 height = 330.0f;
        const Rect panel{ (viewport.x - width) * 0.5f,
                          (viewport.y - height) * 0.5f + (1.0f - fade) * 26.0f, width, height };
        ui.Panel(panel, "Пауза");
        ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const f32 buttonWidth = 260.0f;
        const f32 x = panel.Center().x - buttonWidth * 0.5f;
        f32 y = panel.y + theme.headerHeight + 20.0f;

        auto button = [&](const std::string& label)
        {
            const Rect rect{ x, y, buttonWidth, 34.0f };
            y += 42.0f;
            return ui.Button(rect, label, !m_saveDialogOpen);
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
            ui.RestartTransition("game.savedialog");
        }

        if (button("Зберегти об'єкти карти (F9)"))
        {
            m_status = WorldGenerator::SaveObjects(World::Get(), m_mapFolder)
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
        m_status = SaveGame::Save(World::Get(), clean, m_mapFolder)
            ? "Збережено: " + clean
            : "Не вдалося зберегти";
        m_statusTimer = 4.0f;
    }

    void GameScene::DrawSaveDialog()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();

        const f32 fade = ui.Transition("game.savedialog", m_saveDialogOpen, 0.15f);
        if (fade <= 0.001f) return;

        const Vec2 viewport = renderer.ViewportSize();
        renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, theme.shadow.WithAlpha(0.7f * fade));

        const f32 width = 520.0f;
        const f32 height = 420.0f;
        const Rect panel{ (viewport.x - width) * 0.5f,
                          (viewport.y - height) * 0.5f + (1.0f - fade) * 26.0f, width, height };
        ui.Panel(panel, "Зберегти як");
        ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const Rect body = Rect{ panel.x, panel.y + theme.headerHeight,
                                panel.w, panel.h - theme.headerHeight - 96.0f }.Inset(theme.padding);

        if (m_saves.empty())
        {
            ui.LabelCentered({ body.x, body.y + 30.0f, body.w, 22.0f },
                             "Збережень ще немає", theme.textDim);
        }
        else
        {
            const f32 rowHeight = 40.0f;
            const Rect content = ui.BeginScroll(body, m_saves.size() * rowHeight, m_saveScroll);
            for (size_t i = 0; i < m_saves.size(); ++i)
            {
                const Rect row{ content.x, content.y + i * rowHeight, content.w, rowHeight - 4.0f };
                if (ui.ListItem(row, m_saves[i].name, static_cast<i32>(i) == m_selectedSave))
                {
                    m_selectedSave = static_cast<i32>(i);
                    m_saveName = m_saves[i].name;   // clicking a slot fills the name field
                }
                ui.LabelRight({ row.x, row.y, row.w - theme.padding, row.h },
                              m_saves[i].dateText, theme.textDim);
            }
            ui.EndScroll();
        }

        const f32 fieldY = panel.Bottom() - 84.0f;
        ui.Label({ panel.x + theme.padding, fieldY, 110.0f, 26.0f }, "Назва", theme.textDim);
        ui.TextField({ panel.x + theme.padding + 90.0f, fieldY,
                       panel.w - theme.padding * 2.0f - 90.0f, 26.0f }, "saveName", m_saveName, 40);

        const f32 buttonY = panel.Bottom() - 44.0f;
        const f32 buttonWidth = (panel.w - theme.padding * 3.0f) * 0.5f;

        if (ui.Button({ panel.x + theme.padding, buttonY, buttonWidth, 32.0f }, "Скасувати"))
        {
            m_saveDialogOpen = false;
        }
        if (ui.Button({ panel.x + theme.padding * 2.0f + buttonWidth, buttonY, buttonWidth, 32.0f },
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
        DrawSettlementLabels();
    }

    void GameScene::DrawSettlements()
    {
        World& world = World::Get();
        Renderer& renderer = Renderer::Get();
        const MapData& map = world.Map();
        const Camera& camera = renderer.GetCamera();

        const f32 size = ConfigManager::Get().Float("render/spriteScale/settlement", 26.0f);
        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 160.0f };
        const Vec2 focus = camera.Focus();

        for (const auto& [id, settlement] : world.Settlements())
        {
            if (std::abs(settlement.position.x - focus.x) > half.x) continue;
            if (std::abs(settlement.position.y - focus.y) > half.y) continue;

            const Color tint = settlement.IsIndependent()
                ? Color::FromRGB(0xB9C0C8)
                : ClanColor(settlement.owner);

            const f32 height = map.WorldHeightAtMap(settlement.position);
            const f32 scale = settlement.kind == SettlementKind::Village
                ? size * 0.8f
                : size * (settlement.kind == SettlementKind::City ? 1.15f : 1.0f);

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
            instance.uvRect = renderer.SpriteUV(settlement.Sprite());
            instance.color = tint;
            instance.params = { 0.5f, -1.0f, flash, 0.0f };
            renderer.DrawSpriteRaw(instance);
        }
    }

    void GameScene::DrawCohorts()
    {
        World& world = World::Get();
        Renderer& renderer = Renderer::Get();
        const MapData& map = world.Map();
        const Camera& camera = renderer.GetCamera();

        const f32 size = ConfigManager::Get().Float("render/spriteScale/cohort", 20.0f);
        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 160.0f };
        const Vec2 focus = camera.Focus();

        for (const auto& [id, cohort] : world.Cohorts())
        {
            if (std::abs(cohort.position.x - focus.x) > half.x) continue;
            if (std::abs(cohort.position.y - focus.y) > half.y) continue;

            // A garrison is inside its walls: it is commanded from the settlement panel,
            // and drawing it on the map would only clutter the town it is sitting in.
            if (cohort.garrisonOf != kInvalidId) continue;

            const Vec2 drawPosition = cohort.position;
            const f32 height = map.WorldHeightAtMap(drawPosition);
            const f32 flash = cohort.inBattle ? 0.4f + 0.4f * std::sin(m_pulse * 10.0f) : 0.0f;
            renderer.DrawSprite(SpriteId::Cohort, drawPosition, height, size,
                                ClanColor(cohort.clan), 0.5f, flash);

            // A small race marker above the banner tells you who is marching.
            const Clan* clan = world.FindClan(cohort.clan);
            if (clan)
            {
                const SpriteId raceSprite = RaceDatabase::Get().Race(clan->raceId).sprite;
                SpriteInstance marker;
                marker.worldPosition = Camera::ToWorld(drawPosition, height);
                marker.size = { size * 0.55f, size * 0.55f };
                marker.uvRect = renderer.SpriteUV(raceSprite);
                marker.color = Color::FromRGB(0xFFFFFF);
                marker.params = { -0.75f, 0.0f, 0.0f, 0.0f };
                renderer.DrawSpriteRaw(marker);
            }
        }
    }

    void GameScene::DrawMines()
    {
        World& world = World::Get();
        Renderer& renderer = Renderer::Get();
        const MapData& map = world.Map();
        const f32 size = ConfigManager::Get().Float("render/spriteScale/quarry", 18.0f);

        for (const MineSite& mine : world.Mines())
        {
            const Color tint = mine.developed && mine.owner != kInvalidId
                ? ClanColor(mine.owner)
                : Color::FromRGB(0x9AA3AB);
            renderer.DrawSprite(SpriteId::Quarry, mine.position, map.WorldHeightAtMap(mine.position),
                                size, tint, 0.5f);
        }
    }

    void GameScene::DrawSettlementLabels()
    {
        World& world = World::Get();
        Renderer& renderer = Renderer::Get();
        const Camera& camera = renderer.GetCamera();
        const MapData& map = world.Map();
        const Theme& theme = Theme::Get();
        ConfigManager& config = ConfigManager::Get();

        const Settings& settings = Settings::Get();

        const f32 minZoom = settings.labelMinZoom;
        const f32 scale = config.Float("render/labels/scale", 0.95f);
        const f32 offset = config.Float("render/labels/offset", 6.0f);
        const f32 settlementSize = config.Float("render/spriteScale/settlement", 26.0f);
        const bool showNames = settings.showLabels && camera.Zoom() >= minZoom;

        const Vec2 viewport = renderer.ViewportSize();
        const Vec2 half = camera.VisibleHalfExtent() + Vec2{ 120.0f, 120.0f };
        const Vec2 focus = camera.Focus();

        for (const auto& [id, settlement] : world.Settlements())
        {
            if (std::abs(settlement.position.x - focus.x) > half.x) continue;
            if (std::abs(settlement.position.y - focus.y) > half.y) continue;

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
                const Clan* besieger = world.FindClan(settlement.besiegedBy);
                const std::string text = "ОБЛОГА " + Percent(settlement.siegeProgress);
                const f32 width = renderer.TextWidth(text, scale);
                const Rect box{ screen.x - width * 0.5f - 5.0f,
                                screen.y - iconHalf - offset - renderer.TextHeight(scale),
                                width + 10.0f, renderer.TextHeight(scale) + 6.0f };

                renderer.UIRect(box, theme.shadow.WithAlpha(0.80f));
                renderer.UIRect({ box.x, box.Bottom() - 2.0f, box.w * Clamp01(settlement.siegeProgress), 2.0f },
                                besieger ? besieger->color : theme.negative);
                renderer.UITextCentered(text, box, theme.negative, scale);
            }

            if (!showNames) continue;

            // The name sits under the icon, so the plate never covers the settlement itself.
            const Clan* owner = world.FindClan(settlement.owner);
            const Color color = LabelColor(owner ? owner->color : Color::FromRGB(0xC9D2DA));
            const f32 width = renderer.TextWidth(settlement.name, scale);
            const Rect plate{ screen.x - width * 0.5f - 4.0f, screen.y + iconHalf + 2.0f,
                              width + 8.0f, renderer.TextHeight(scale) + 4.0f };

            renderer.UIRect(plate, theme.shadow.WithAlpha(0.72f));
            renderer.UITextCentered(settlement.name, plate, color, scale);
        }
    }

    void GameScene::DrawSelectionMarkers()
    {
        World& world = World::Get();
        Renderer& renderer = Renderer::Get();
        const MapData& map = world.Map();

        auto ring = [&](const Vec2& position, f32 size, const Color& color, f32 alpha)
        {
            renderer.DrawSprite(SpriteId::Circle, position, map.WorldHeightAtMap(position),
                                size, color.WithAlpha(alpha), 0.5f);
        };

        if (m_hoveredSettlement != kInvalidId)
        {
            if (const Settlement* settlement = world.FindSettlement(m_hoveredSettlement))
            {
                ring(settlement->position, 34.0f, Theme::Get().textStrong, 0.28f);
            }
        }
        if (m_hoveredCohort != kInvalidId)
        {
            if (const Cohort* cohort = world.FindCohort(m_hoveredCohort))
            {
                ring(cohort->position, 28.0f, Theme::Get().textStrong, 0.28f);
            }
        }

        const f32 pulse = 0.45f + 0.2f * std::sin(m_pulse * 4.0f);
        if (m_selectionKind == SelectionKind::Settlement)
        {
            if (const Settlement* settlement = world.FindSettlement(m_selected))
            {
                ring(settlement->position, 40.0f, Theme::Get().selection, pulse);
            }
        }
        else if (m_selectionKind == SelectionKind::Cohort)
        {
            if (const Cohort* cohort = world.FindCohort(m_selected))
            {
                ring(cohort->position, 32.0f, Theme::Get().selection, pulse);
            }
        }
    }

    void GameScene::DrawOrderPreview()
    {
        if (m_selectionKind != SelectionKind::Cohort) return;

        World& world = World::Get();
        Renderer& renderer = Renderer::Get();
        const Camera& camera = renderer.GetCamera();
        const Theme& theme = Theme::Get();

        const Cohort* cohort = world.FindCohort(m_selected);
        if (!cohort) return;

        // Draw the planned march as a screen-space polyline so it stays visible at any zoom.
        const Task& task = cohort->currentTask;
        if (task.waypointIndex < task.waypoints.size())
        {
            Vec2 previous = camera.MapToScreen(cohort->position, world.Map().WorldHeightAtMap(cohort->position));
            for (size_t i = task.waypointIndex; i < task.waypoints.size(); ++i)
            {
                const Vec2 point = camera.MapToScreen(task.waypoints[i],
                                                      world.Map().WorldHeightAtMap(task.waypoints[i]));
                renderer.UILine(previous, point, theme.selection.WithAlpha(0.75f), 2.0f);
                previous = point;
            }
            renderer.UIRect({ previous.x - 4.0f, previous.y - 4.0f, 8.0f, 8.0f }, theme.selection);
        }
    }
}
