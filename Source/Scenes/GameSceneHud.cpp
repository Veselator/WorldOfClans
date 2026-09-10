// GameSceneHud.cpp - every panel of the in-game interface.
//
// Split out of GameScene.cpp so the scene file stays about the world and this one stays
// about presenting it. All of it is immediate-mode: what you see is what this frame drew.
#include "GameScene.h"
#include "SceneManager.h"

#include "../Core/Config.h"
#include "../Game/Factories/EvaluatorFactory.h"
#include "../Game/Systems/BattleSystem.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/Systems/DiplomacySystem.h"
#include "../Game/Systems/DynastySystem.h"
#include "../Game/Systems/EconomySystem.h"
#include "../Game/Systems/MovementSystem.h"
#include "../Game/Systems/PopulationSystem.h"
#include "../Game/Systems/SettlementSystem.h"
#include "../Game/Systems/Simulation.h"
#include "../Game/World/RaceDatabase.h"
#include "../Game/World/World.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cstdio>

namespace woc
{
    namespace
    {
        std::string Percent(f32 value01)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", Clamp01(value01) * 100.0f);
            return buffer;
        }

        std::string Signed(f32 value)
        {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%+.1f", value);
            return buffer;
        }

        Color ValueColor(f32 value01, const Theme& theme)
        {
            if (value01 > 0.66f) return theme.positive;
            if (value01 > 0.33f) return theme.warning;
            return theme.negative;
        }
    }

    // =====================================================================================
    // Top bar
    // =====================================================================================

    void GameScene::DrawTopBar()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();
        Simulation& simulation = Simulation::Get();

        const Vec2 viewport = renderer.ViewportSize();
        const Rect bar{ 0.0f, 0.0f, viewport.x, theme.topBarHeight };

        renderer.UIRect(bar, theme.panelHeader.WithAlpha(0.97f));
        renderer.UIRect({ 0.0f, bar.Bottom() - 1.0f, viewport.x, 1.0f }, theme.border);
        ui.BlockMouse(bar);

        f32 x = 12.0f;

        // --- realm ---------------------------------------------------------------------------
        const State* state = world.HumanState();
        Clan* clan = world.HumanClan();
        if (state)
        {
            renderer.UIRect({ x, 7.0f, 14.0f, 14.0f }, state->color);
            x += 20.0f;
            renderer.UIText(state->name, { x, (theme.topBarHeight - renderer.TextHeight()) * 0.5f },
                            theme.textStrong);
            x += renderer.TextWidth(state->name) + 24.0f;
        }

        // --- treasury -------------------------------------------------------------------------
        if (clan)
        {
            const ClanBudget budget = EconomySystem::Get().Preview(world, clan->id);
            struct Entry { const char* labelKey; f32 stock; f32 net; };
            const Entry entries[] = {
                { "money", clan->resources.money, budget.net.money },
                { "food",  clan->resources.food,  budget.net.food  },
                { "wood",  clan->resources.wood,  budget.net.wood  },
                { "stone", clan->resources.stone, budget.net.stone },
            };

            for (const Entry& entry : entries)
            {
                const std::string label = theme.Label(entry.labelKey);
                renderer.UIText(label + ":", { x, (theme.topBarHeight - renderer.TextHeight()) * 0.5f },
                                theme.textDim);
                x += renderer.TextWidth(label) + 10.0f;

                const std::string amount = FormatNumber(entry.stock);
                renderer.UIText(amount, { x, (theme.topBarHeight - renderer.TextHeight()) * 0.5f },
                                theme.textStrong);
                x += renderer.TextWidth(amount) + 6.0f;

                const std::string delta = "(" + Signed(entry.net) + ")";
                renderer.UIText(delta, { x, (theme.topBarHeight - renderer.TextHeight()) * 0.5f },
                                entry.net >= 0.0f ? theme.positive : theme.negative);
                x += renderer.TextWidth(delta) + 22.0f;
            }
        }

        // --- clock -----------------------------------------------------------------------------
        const std::string date = world.Time().ToString();
        const f32 speedWidth = 30.0f;
        const f32 speedCount = static_cast<f32>(simulation.SpeedSteps().size());
        const f32 speedBlock = speedWidth * speedCount + 8.0f;

        const f32 dateX = viewport.x - speedBlock - renderer.TextWidth(date) - 24.0f;
        renderer.UIText(date, { dateX, (theme.topBarHeight - renderer.TextHeight()) * 0.5f }, theme.accent);

        f32 speedX = viewport.x - speedBlock - 8.0f;
        for (size_t i = 0; i < simulation.SpeedSteps().size(); ++i)
        {
            const Rect button{ speedX, 3.0f, speedWidth - 3.0f, theme.topBarHeight - 6.0f };
            const bool active = static_cast<i32>(i) == simulation.SpeedIndex();

            // Label the buttons with the speed they actually set, not their position.
            const f32 multiplier = simulation.SpeedSteps()[i];
            std::string label = "||";
            if (multiplier > 0.0f)
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer),
                              multiplier < 1.0f ? "%.1f" : "%.0f", multiplier);
                label = buffer;
            }

            renderer.UIRect(button, active ? theme.accent.WithAlpha(0.35f) : theme.panel);
            renderer.UIRectOutline(button, active ? theme.accent : theme.border, 1.0f);
            renderer.UITextCentered(label, button, active ? theme.textStrong : theme.textDim);
            if (ui.InvisibleButton(button, "speed" + label)) simulation.SetSpeedIndex(static_cast<i32>(i));
            speedX += speedWidth;
        }
    }

    // =====================================================================================
    // Chronicle strip and status line
    // =====================================================================================

    void GameScene::DrawChronicle()
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        const Vec2 viewport = renderer.ViewportSize();
        const std::vector<Chronicle>& entries = world.ChronicleEntries();

        const size_t shown = std::min<size_t>(entries.size(), 5);
        f32 y = viewport.y - theme.bottomBarHeight - 14.0f - shown * (renderer.TextHeight() + 3.0f);

        for (size_t i = entries.size() - shown; i < entries.size(); ++i)
        {
            // Older lines fade out rather than disappearing abruptly.
            const f32 age = static_cast<f32>(entries.size() - i) / static_cast<f32>(shown + 1);
            renderer.UIText(entries[i].text, { 12.0f, y }, entries[i].color.WithAlpha(1.0f - age * 0.55f));
            y += renderer.TextHeight() + 3.0f;
        }

        if (m_statusTimer > 0.0f && !m_status.empty())
        {
            const f32 width = renderer.TextWidth(m_status) + 24.0f;
            const Rect box{ (viewport.x - width) * 0.5f, viewport.y * 0.16f, width, 28.0f };
            renderer.UIRect(box, theme.panel.WithAlpha(0.9f * std::min(1.0f, m_statusTimer)));
            renderer.UIRectOutline(box, theme.accent.WithAlpha(std::min(1.0f, m_statusTimer)), 1.0f);
            renderer.UITextCentered(m_status, box, theme.textStrong.WithAlpha(std::min(1.0f, m_statusTimer)));
        }
    }

    // =====================================================================================
    // Bottom bar: the selected army's units
    // =====================================================================================

    void GameScene::DrawBottomBar()
    {
        if (m_selectionKind != SelectionKind::Cohort) return;

        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        Cohort* cohort = world.FindCohort(m_selected);
        if (!cohort) return;

        const Vec2 viewport = renderer.ViewportSize();
        const f32 lift = ui.SlideIn("game.bottom", true, theme.bottomBarHeight, 0.20f);
        const Rect bar{ 0.0f, viewport.y - theme.bottomBarHeight + lift,
                        viewport.x - theme.sidebarWidth - 8.0f, theme.bottomBarHeight };
        ui.Panel(bar);

        const Rect header{ bar.x + theme.padding, bar.y + 4.0f, bar.w - theme.padding * 2.0f, 18.0f };
        renderer.UIText(cohort->DisplayName() + "  —  " + Task::TypeName(cohort->currentTask.type),
                        { header.x, header.y }, theme.accent);

        const std::string summary = "Досвід " + Percent(cohort->experience) +
                                    "   Постачання " + Percent(cohort->supply) +
                                    "   Усього " + std::to_string(world.CohortStrength(cohort->id)) + " чол.";
        ui.LabelRight(header, summary, theme.textDim);

        // One card per unit; click to inspect its people.
        const f32 cardWidth = 118.0f;
        const f32 cardHeight = theme.bottomBarHeight - 32.0f;
        f32 x = bar.x + theme.padding;

        for (EntityId unitId : cohort->units)
        {
            Unit* unit = world.FindUnit(unitId);
            if (!unit) continue;
            if (x + cardWidth > bar.Right() - theme.padding) break;

            const Rect card{ x, bar.y + 26.0f, cardWidth - 6.0f, cardHeight };
            const bool selected = unitId == m_selectedUnit;

            renderer.UIRect(card, selected ? theme.panelAlt : theme.panel);
            renderer.UIRectOutline(card, selected ? theme.accent : theme.border, 1.0f);

            const RoleInfo& role = UnitDatabase::Get().Role(unit->role);
            renderer.UIText(role.name, { card.x + 6.0f, card.y + 5.0f }, theme.text);

            const std::string strength = std::to_string(unit->Strength()) + "/" +
                                         std::to_string(unit->establishment);
            renderer.UIText(strength, { card.x + 6.0f, card.y + 22.0f }, theme.textStrong);

            ui.ProgressBar({ card.x + 6.0f, card.y + 40.0f, card.w - 12.0f, 7.0f },
                           unit->StrengthFraction(), ValueColor(unit->StrengthFraction(), theme));
            ui.ProgressBar({ card.x + 6.0f, card.y + 50.0f, card.w - 12.0f, 7.0f },
                           unit->morale, theme.accent);

            if (ui.InvisibleButton(card, "unitcard" + std::to_string(unitId)))
            {
                m_selectedUnit = unitId;
                m_selectedCharacter = kInvalidId;
                m_panelMode = PanelMode::Selection;
            }
            ui.TooltipIfHovered(card, role.name + "\n" + role.description +
                                "\nДух: " + Percent(unit->morale) +
                                "  Вишкіл: " + Percent(unit->training) +
                                "  Втома: " + Percent(unit->fatigue));
            x += cardWidth;
        }
    }

    // =====================================================================================
    // Right-hand panel
    // =====================================================================================

    void GameScene::DrawSidePanel()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        const Vec2 viewport = renderer.ViewportSize();
        const Rect panel{ viewport.x - theme.sidebarWidth, theme.topBarHeight,
                          theme.sidebarWidth, viewport.y - theme.topBarHeight };
        ui.Panel(panel);

        // --- tabs -------------------------------------------------------------------------
        const char* tabs[] = { "Вибір", "Держава", "Дипломатія", "Хроніка" };
        const f32 tabWidth = panel.w / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const Rect tab{ panel.x + i * tabWidth, panel.y, tabWidth, theme.headerHeight };
            const bool active = static_cast<int>(m_panelMode) == i;
            renderer.UIRect(tab, active ? theme.panelAlt : theme.panelHeader);
            renderer.UITextCentered(tabs[i], tab, active ? theme.accent : theme.textDim);
            if (active) renderer.UIRect({ tab.x, tab.Bottom() - 2.0f, tab.w, 2.0f }, theme.accent);
            if (ui.InvisibleButton(tab, tabs[i])) m_panelMode = static_cast<PanelMode>(i);
        }

        // Whenever the panel changes subject, its content slides in from the right.
        const std::string key = std::to_string(static_cast<int>(m_panelMode)) + ":" +
                                std::to_string(static_cast<int>(m_selectionKind)) + ":" +
                                std::to_string(m_selected) + ":" + std::to_string(m_selectedUnit) +
                                ":" + std::to_string(m_selectedCharacter);
        if (key != m_panelKey)
        {
            m_panelKey = key;
            ui.RestartTransition("game.side");
        }
        const f32 slide = ui.SlideIn("game.side", true, 22.0f, 0.16f);

        const Rect body{ panel.x + slide, panel.y + theme.headerHeight,
                         panel.w, panel.h - theme.headerHeight };

        switch (m_panelMode)
        {
        case PanelMode::Realm:      DrawRealmPanel(body); return;
        case PanelMode::Diplomacy:  DrawDiplomacyPanel(body); return;
        case PanelMode::Chronicle:  DrawChroniclePanel(body); return;
        default: break;
        }

        if (m_selectionKind == SelectionKind::Settlement)
        {
            if (Settlement* settlement = world.FindSettlement(m_selected))
            {
                DrawSettlementPanel(body, *settlement);
                return;
            }
            // The subject is gone (razed, or an army wiped out): forget it, so Escape and
            // the panel agree about what is selected.
            m_selectionKind = SelectionKind::None;
            m_selected = kInvalidId;
        }
        else if (m_selectionKind == SelectionKind::Cohort)
        {
            if (Cohort* cohort = world.FindCohort(m_selected))
            {
                DrawCohortPanel(body, *cohort);
                return;
            }
            m_selectionKind = SelectionKind::None;
            m_selected = kInvalidId;
            m_selectedUnit = kInvalidId;
            m_selectedCharacter = kInvalidId;
        }

        ui.LabelCentered({ body.x, body.y + 40.0f, body.w, 24.0f },
                         "Нічого не вибрано", theme.textDim);
        ui.LabelCentered({ body.x, body.y + 66.0f, body.w, 24.0f },
                         "ЛКМ — вибрати, ПКМ — наказ", theme.textDim);
    }

    // =====================================================================================
    // Settlement panel
    // =====================================================================================

    void GameScene::DrawSettlementPanel(const Rect& area, Settlement& settlement)
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();
        SettlementSystem& settlements = SettlementSystem::Get();

        const RaceDatabase& races = RaceDatabase::Get();
        const Clan* owner = world.FindClan(settlement.owner);
        const bool playerOwns = owner && world.HumanState() && owner->state == world.HumanState()->id;

        // Measure the content so the scroll view knows how tall it is.
        const std::vector<BuildOption> buildOptions = settlements.BuildOptions(world, settlement.id);
        const std::vector<RecruitOption> recruitOptions = settlements.RecruitOptions(world, settlement.id);

        f32 contentHeight = 500.0f;
        contentHeight += buildOptions.size() * 26.0f;
        contentHeight += recruitOptions.size() * 26.0f;
        contentHeight += races.Faiths().size() * 24.0f;

        const Rect view = area.Inset(theme.padding);
        const Rect content = ui.BeginScroll(view, contentHeight, m_sidePanelScroll);

        f32 y = content.y;
        const f32 rowHeight = theme.rowHeight;
        auto row = [&]() { const Rect r{ content.x, y, content.w, rowHeight }; y += rowHeight + 2.0f; return r; };

        // --- identity -------------------------------------------------------------------------
        renderer.UISprite(settlement.Sprite(), { content.x, y, 32.0f, 32.0f },
                          owner ? owner->color : Color::FromRGB(0xB9C0C8));
        renderer.UIText(settlement.name, { content.x + 40.0f, y + 2.0f }, theme.textStrong, 1.2f);
        renderer.UIText(settlement.TierName(), { content.x + 40.0f, y + 20.0f }, theme.textDim);
        y += 40.0f;

        ui.KeyValue(row(), theme.Label("owner"), owner ? owner->name : "Незалежне",
                    owner ? owner->color : theme.textDim);
        ui.KeyValue(row(), theme.Label("race"), races.Race(settlement.raceId).name, theme.text);
        ui.KeyValue(row(), theme.Label("faith"), races.Faith(settlement.faithId).name,
                    races.Faith(settlement.faithId).color);
        ui.KeyValue(row(), theme.Label("population"), std::to_string(settlement.population), theme.text);

        // --- meters ----------------------------------------------------------------------------
        {
            const Rect r = row();
            ui.Label(r, theme.Label("prosperity"), theme.textDim);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, settlement.prosperity,
                           ValueColor(settlement.prosperity, theme), Percent(settlement.prosperity));
        }
        {
            const Rect r = row();
            ui.Label(r, theme.Label("loyalty"), theme.textDim);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, settlement.loyalty,
                           ValueColor(settlement.loyalty, theme), Percent(settlement.loyalty));
            const f32 forecast = PopulationSystem::Get().LoyaltyForecast(world, settlement.id);
            ui.TooltipIfHovered(r, "Зміна за місяць: " + Signed(forecast * 100.0f) + " %");
        }

        // --- output ------------------------------------------------------------------------------
        y += 6.0f;
        ui.Label(row(), "ВИРОБНИЦТВО ЗА МІСЯЦЬ", theme.accent);
        const ResourceData output = EconomySystem::Get().SettlementOutput(world, settlement.id);
        ui.KeyValue(row(), theme.Label("food"), FormatNumber(output.food), theme.text);
        ui.KeyValue(row(), theme.Label("money"), FormatNumber(output.money), theme.text);
        ui.KeyValue(row(), theme.Label("wood"), FormatNumber(output.wood), theme.text);
        ui.KeyValue(row(), theme.Label("stone"), FormatNumber(output.stone), theme.text);

        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(settlement, world.Map());
        ui.KeyValue(row(), theme.Label("coverage"), FormatNumber(evaluator->Coverage()), theme.text);
        ui.KeyValue(row(), theme.Label("defense"), FormatNumber(
            BattleSystem::Get().SettlementDefense(world, settlement.id)), theme.text);

        if (settlement.besiegedBy != kInvalidId)
        {
            const Rect r = row();
            ui.Label(r, "ОБЛОГА", theme.negative);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f },
                           settlement.siegeProgress, theme.negative, Percent(settlement.siegeProgress));
        }

        // --- garrison -------------------------------------------------------------------------------
        y += 8.0f;
        ui.Label(row(), theme.Label("garrison"), theme.accent);
        {
            bool anyGarrison = false;
            for (const auto& [cohortId, cohort] : world.Cohorts())
            {
                if (cohort.garrisonOf != settlement.id) continue;
                anyGarrison = true;

                const Rect r = row();
                if (ui.ListItem(r, cohort.DisplayName(), cohortId == m_selected,
                                ClanColor(cohort.clan)))
                {
                    m_selectionKind = SelectionKind::Cohort;
                    m_selected = cohortId;
                    m_selectedUnit = kInvalidId;
                    m_selectedCharacter = kInvalidId;
                }
                ui.LabelRight(r, std::to_string(world.CohortStrength(cohortId)) + " чол.", theme.textDim);
            }
            if (!anyGarrison) ui.Label(row(), "Немає війська", theme.textDim);
        }

        // --- buildings ------------------------------------------------------------------------------
        y += 8.0f;
        ui.Label(row(), "БУДІВЛІ", theme.accent);

        if (settlement.buildings.empty() && settlement.construction.empty())
        {
            ui.Label(row(), "Нічого не збудовано", theme.textDim);
        }
        for (const std::string& buildingId : settlement.buildings)
        {
            const BuildingInfo* info = BuildingDatabase::Get().Find(buildingId);
            const Rect r = row();
            ui.Label(r, "+ " + (info ? info->name : buildingId), theme.text);
            if (info) ui.TooltipIfHovered(r, info->description);
        }
        for (const ConstructionOrder& order : settlement.construction)
        {
            const BuildingInfo* info = BuildingDatabase::Get().Find(order.buildingId);
            const Rect r = row();
            ui.Label(r, "* " + (info ? info->name : order.buildingId), theme.warning);
            ui.LabelRight(r, std::to_string(order.daysRemaining) + " дн.", theme.textDim);
            if (playerOwns && ui.Button({ r.Right() - 22.0f, r.y, 20.0f, r.h }, "x"))
            {
                settlements.CancelConstruction(world, settlement.id, order.buildingId);
            }
        }

        if (playerOwns)
        {
            y += 6.0f;
            ui.Label(row(), "ЗВЕСТИ", theme.accent);
            for (const BuildOption& option : buildOptions)
            {
                if (!option.building) continue;
                const Rect r{ content.x, y, content.w, 24.0f };
                y += 26.0f;

                const bool enabled = option.allowed && option.affordable;
                if (ui.Button(r, option.building->name, enabled))
                {
                    settlements.StartConstruction(world, settlement.id, option.building->id);
                }

                const ResourceData& cost = option.building->cost;
                std::string tooltip = option.building->description + "\n\nЦіна: " +
                    FormatNumber(cost.money) + " срібла, " + FormatNumber(cost.wood) + " дерева, " +
                    FormatNumber(cost.stone) + " каменю\nТермін: " + std::to_string(option.building->buildDays) + " дн.";
                if (!enabled && !option.blockedReason.empty()) tooltip += "\n\n" + option.blockedReason;
                ui.TooltipIfHovered(r, tooltip);
            }

            // --- recruitment -----------------------------------------------------------------------
            y += 8.0f;
            ui.Label(row(), "НАБІР ДО КОГОРТИ", theme.accent);
            for (const RecruitOption& option : recruitOptions)
            {
                const Rect r{ content.x, y, content.w, 24.0f };
                y += 26.0f;

                const std::string label = option.name + "  (" + std::to_string(option.headCount) + ")";
                if (ui.Button(r, label, option.affordable))
                {
                    EntityId cohortId = kInvalidId;
                    if (m_selectionKind == SelectionKind::Cohort) cohortId = m_selected;
                    else
                    {
                        for (const auto& [id, cohort] : world.Cohorts())
                        {
                            if (cohort.garrisonOf == settlement.id) { cohortId = id; break; }
                        }
                    }
                    const EntityId result = settlements.Recruit(world, settlement.id, cohortId, option.role);
                    if (result != kInvalidId)
                    {
                        m_status = "Загін набрано";
                        m_statusTimer = 2.5f;
                    }
                }
                std::string tooltip = "Ціна: " + FormatNumber(option.cost) + " срібла";
                if (!option.affordable && !option.blockedReason.empty())
                {
                    tooltip += "\n" + option.blockedReason;
                }
                ui.TooltipIfHovered(r, tooltip);
            }

            // --- faith --------------------------------------------------------------------------------
            y += 8.0f;
            ui.Label(row(), "НАВЕРНУТИ ДО ВІРИ", theme.accent);
            for (const FaithInfo& faith : races.Faiths())
            {
                if (faith.id == settlement.faithId) continue;
                const Rect r{ content.x, y, content.w, 22.0f };
                y += 24.0f;

                const f32 cost = settlements.ConversionCost(world, settlement.id, faith.id);
                const bool affordable = owner && owner->resources.money >= cost &&
                                        settlement.conversionDaysLeft == 0;
                if (ui.Button(r, faith.name + " (" + FormatNumber(cost) + ")", affordable))
                {
                    settlements.StartConversion(world, settlement.id, faith.id);
                }
            }
            if (settlement.conversionDaysLeft > 0)
            {
                ui.KeyValue(row(), "Навернення", std::to_string(settlement.conversionDaysLeft) + " дн.",
                            theme.warning);
            }

            // --- lordly actions ------------------------------------------------------------------------
            y += 8.0f;
            ui.Label(row(), "ДІЇ", theme.accent);
            {
                const Rect r{ content.x, y, content.w * 0.5f - 3.0f, 24.0f };
                const Rect r2{ content.x + content.w * 0.5f + 3.0f, y, content.w * 0.5f - 3.0f, 24.0f };
                y += 28.0f;

                if (ui.Button(r, "Відпустити"))
                {
                    settlements.GrantIndependence(world, settlement.id);
                }
                if (ui.Button(r2, "Спалити", settlement.kind == SettlementKind::Village))
                {
                    settlements.Raze(world, settlement.id, owner ? owner->id : kInvalidId);
                    m_selectionKind = SelectionKind::None;
                    m_selected = kInvalidId;
                }
            }
        }

        ui.EndScroll();
    }

    // =====================================================================================
    // Cohort, unit and character panels
    // =====================================================================================

    void GameScene::DrawCohortPanel(const Rect& area, Cohort& cohort)
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        // A selected unit takes over the panel to show its people.
        if (m_selectedUnit != kInvalidId)
        {
            if (Unit* unit = world.FindUnit(m_selectedUnit))
            {
                DrawUnitDetails(area, *unit);
                return;
            }
            m_selectedUnit = kInvalidId;
        }

        const Clan* clan = world.FindClan(cohort.clan);
        const Rect view = area.Inset(theme.padding);

        f32 y = view.y;
        auto row = [&](f32 height = 0.0f)
        {
            const f32 h = height > 0.0f ? height : theme.rowHeight;
            const Rect r{ view.x, y, view.w, h };
            y += h + 2.0f;
            return r;
        };

        renderer.UISprite(SpriteId::Cohort, { view.x, y, 32.0f, 32.0f },
                          clan ? clan->color : theme.textDim);
        renderer.UIText(cohort.DisplayName(), { view.x + 40.0f, y + 2.0f }, theme.textStrong, 1.2f);
        renderer.UIText(clan ? clan->name : "—", { view.x + 40.0f, y + 20.0f }, theme.textDim);
        y += 40.0f;

        ui.KeyValue(row(), "Наказ", Task::TypeName(cohort.currentTask.type), theme.text);
        ui.KeyValue(row(), theme.Label("units"), std::to_string(cohort.units.size()) + " / " +
                    std::to_string(UnitDatabase::Get().MaxUnitsPerCohort()), theme.text);
        ui.KeyValue(row(), "Усього людей", std::to_string(world.CohortStrength(cohort.id)), theme.text);

        {
            const Rect r = row();
            ui.Label(r, theme.Label("experience"), theme.textDim);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, cohort.experience,
                           theme.accent, Percent(cohort.experience));
        }
        {
            const Rect r = row();
            ui.Label(r, "Постачання", theme.textDim);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, cohort.supply,
                           ValueColor(cohort.supply, theme), Percent(cohort.supply));
        }

        const f32 power = world.CohortPower(cohort.id);
        ui.KeyValue(row(), "Бойова сила", FormatNumber(power), theme.text);

        if (cohort.garrisonOf != kInvalidId)
        {
            if (const Settlement* home = world.FindSettlement(cohort.garrisonOf))
            {
                ui.KeyValue(row(), "У гарнізоні", home->name, theme.positive);
            }
        }

        y += 8.0f;
        ui.Label(row(), theme.Label("units"), theme.accent);

        for (EntityId unitId : cohort.units)
        {
            Unit* unit = world.FindUnit(unitId);
            if (!unit) continue;

            const Rect r = row(34.0f);
            const RoleInfo& role = UnitDatabase::Get().Role(unit->role);
            if (ui.ListItem(r, role.name, unitId == m_selectedUnit))
            {
                m_selectedUnit = unitId;
                m_selectedCharacter = kInvalidId;
            }
            ui.LabelRight({ r.x, r.y, r.w - 6.0f, 18.0f },
                          std::to_string(unit->Strength()) + " чол.", theme.textStrong);
            ui.ProgressBar({ r.x + 8.0f, r.Bottom() - 10.0f, r.w - 16.0f, 5.0f },
                           unit->StrengthFraction(), ValueColor(unit->StrengthFraction(), theme));
        }

        // --- orders --------------------------------------------------------------------------------
        const State* humanState = world.HumanState();
        if (clan && humanState && clan->state == humanState->id)
        {
            y += 10.0f;
            const Rect stop = row(26.0f);
            if (ui.Button(stop, "Зупинити"))
            {
                cohort.currentTask.Clear();
            }

            const Rect garrison = row(26.0f);
            const Settlement* nearest = world.NearestSettlement(cohort.position, 1e9f, clan->id);
            if (ui.Button(garrison, nearest ? "У гарнізон: " + nearest->name : "У гарнізон", nearest != nullptr))
            {
                if (nearest)
                {
                    MovementSystem::Get().OrderTask(world, cohort.id, TaskType::Garrison,
                                                    nearest->position, nearest->id);
                }
            }
        }
    }

    void GameScene::DrawUnitDetails(const Rect& area, Unit& unit)
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        if (m_selectedCharacter != kInvalidId)
        {
            if (const Character* character = world.FindCharacter(m_selectedCharacter))
            {
                DrawCharacterDetails(area, *character);
                return;
            }
            m_selectedCharacter = kInvalidId;
        }

        const Rect view = area.Inset(theme.padding);
        const Rect back{ view.x, view.y, 90.0f, 22.0f };
        if (ui.Button(back, "< Назад")) m_selectedUnit = kInvalidId;

        const UnitData& stats = unit.Stats();
        const RoleInfo& role = UnitDatabase::Get().Role(unit.role);

        f32 y = view.y + 30.0f;
        auto row = [&]() { const Rect r{ view.x, y, view.w, theme.rowHeight }; y += theme.rowHeight + 2.0f; return r; };

        renderer.UIText(stats.name, { view.x, y }, theme.textStrong, 1.2f);
        y += 24.0f;
        renderer.UIText(role.name, { view.x, y }, theme.textDim);
        y += 22.0f;

        ui.KeyValue(row(), "Чисельність", std::to_string(unit.Strength()) + " / " +
                    std::to_string(unit.establishment), theme.text);
        ui.KeyValue(row(), "Атака", FormatNumber(stats.attack), theme.text);
        ui.KeyValue(row(), "Захист", FormatNumber(stats.defense), theme.text);
        ui.KeyValue(row(), "Здоров'я", FormatNumber(stats.health), theme.text);
        ui.KeyValue(row(), "Швидкість", FormatNumber(stats.speed), theme.text);
        ui.KeyValue(row(), "Дальність", FormatNumber(stats.range), theme.text);
        ui.KeyValue(row(), "Утримання", FormatNumber(stats.upkeep), theme.text);

        {
            const Rect r = row();
            ui.Label(r, theme.Label("morale"), theme.textDim);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, unit.morale,
                           ValueColor(unit.morale, theme), Percent(unit.morale));
        }
        {
            const Rect r = row();
            ui.Label(r, theme.Label("training"), theme.textDim);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, unit.training,
                           theme.accent, Percent(unit.training));
        }
        {
            const Rect r = row();
            ui.Label(r, "Втома", theme.textDim);
            ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, unit.fatigue,
                           theme.warning, Percent(unit.fatigue));
        }

        y += 8.0f;
        ui.Label(row(), theme.Label("characters") + " (" + std::to_string(unit.Strength()) + ")",
                 theme.accent);

        // The whole roster, by name: this is the point of the design.
        const Rect listArea{ view.x, y, view.w, view.Bottom() - y };
        const f32 rowHeight = 20.0f;
        const Rect content = ui.BeginScroll(listArea, unit.characters.size() * rowHeight, m_sidePanelScroll);

        for (size_t i = 0; i < unit.characters.size(); ++i)
        {
            const Character* person = world.FindCharacter(unit.characters[i]);
            if (!person) continue;

            const Rect r{ content.x, content.y + i * rowHeight, content.w, rowHeight - 1.0f };
            if (ui.ListItem(r, person->FullName(), false))
            {
                m_selectedCharacter = person->id;
            }
            ui.LabelRight(r, std::to_string(person->age), theme.textDim);
        }
        ui.EndScroll();
    }

    void GameScene::DrawCharacterDetails(const Rect& area, const Character& character)
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        const Rect view = area.Inset(theme.padding);
        const Rect back{ view.x, view.y, 90.0f, 22.0f };
        if (ui.Button(back, "< Назад")) m_selectedCharacter = kInvalidId;

        const RaceInfo& race = RaceDatabase::Get().Race(character.raceId);

        f32 y = view.y + 32.0f;
        renderer.UISprite(race.sprite, { view.x, y, 40.0f, 40.0f }, race.color);
        renderer.UIText(character.FullName(), { view.x + 48.0f, y + 4.0f }, theme.textStrong, 1.15f);
        renderer.UIText(race.name, { view.x + 48.0f, y + 24.0f }, theme.textDim);
        y += 52.0f;

        auto row = [&]() { const Rect r{ view.x, y, view.w, theme.rowHeight }; y += theme.rowHeight + 2.0f; return r; };

        char buffer[48];
        ui.KeyValue(row(), theme.Label("age"), std::to_string(character.age) + " років", theme.text);
        std::snprintf(buffer, sizeof(buffer), "%.0f см", character.height);
        ui.KeyValue(row(), theme.Label("height"), buffer, theme.text);
        std::snprintf(buffer, sizeof(buffer), "%.0f кг", character.weight);
        ui.KeyValue(row(), theme.Label("weight"), buffer, theme.text);
        ui.KeyValue(row(), theme.Label("origin"), character.origin.empty() ? "—" : character.origin, theme.text);
        ui.KeyValue(row(), "Стать", character.gender == Gender::Male ? "чоловіча" : "жіноча", theme.text);

        if (!character.traits.empty())
        {
            y += 6.0f;
            ui.Label(row(), "РИСИ", theme.accent);
            for (const std::string& traitId : character.traits)
            {
                const Trait* trait = UnitDatabase::Get().FindTrait(traitId);
                const Rect r = row();
                ui.Label(r, "· " + (trait ? trait->name : traitId), theme.text);
                if (trait)
                {
                    std::string effects;
                    auto append = [&effects](const char* label, f32 value)
                    {
                        if (value == 0.0f) return;
                        char temp[48];
                        std::snprintf(temp, sizeof(temp), "%s %+.0f%%  ", label, value * 100.0f);
                        effects += temp;
                    };
                    append("атака", trait->attack);
                    append("захист", trait->defense);
                    append("здоров'я", trait->health);
                    append("дух", trait->morale);
                    append("вишкіл", trait->training);
                    ui.TooltipIfHovered(r, effects.empty() ? trait->name : effects);
                }
            }
        }

        // --- dynasty ------------------------------------------------------------------------------
        if (character.noble)
        {
            y += 8.0f;
            ui.Label(row(), "РІД", theme.accent);
            if (const Clan* clan = world.FindClan(character.clan))
            {
                ui.KeyValue(row(), "Дім", clan->name, clan->color);
                ui.KeyValue(row(), "Голова", clan->head == character.id ? "так" : "немає", theme.text);
            }

            auto namedRelative = [&](const char* label, EntityId id)
            {
                if (id == kInvalidId) return;
                const Character* relative = world.FindCharacter(id);
                if (!relative) return;
                ui.KeyValue(row(), label, relative->FullName() +
                            (relative->alive ? "" : " (†)"), theme.text);
            };
            namedRelative("Батько", character.father);
            namedRelative("Мати", character.mother);
            namedRelative("Дружина/чоловік", character.spouse);

            if (!character.children.empty())
            {
                ui.Label(row(), "Діти", theme.textDim);
                for (EntityId childId : character.children)
                {
                    if (const Character* child = world.FindCharacter(childId))
                    {
                        ui.Label(row(), "  · " + child->FullName() + ", " +
                                 std::to_string(child->age), theme.text);
                    }
                }
            }
        }
    }

    // =====================================================================================
    // Realm, diplomacy and chronicle panels
    // =====================================================================================

    void GameScene::DrawRealmPanel(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        const State* state = world.HumanState();
        if (!state)
        {
            ui.LabelCentered({ area.x, area.y + 30.0f, area.w, 24.0f }, "Держава впала", theme.negative);
            return;
        }

        // Aggregate the realm's totals across all of its houses.
        i32 population = 0;
        u32 soldiers = 0;
        u32 tiles = 0;
        size_t settlementCount = 0;
        size_t cohortCount = 0;
        ResourceData treasury;
        ResourceData net;

        for (EntityId clanId : state->clans)
        {
            const Clan* clan = world.FindClan(clanId);
            if (!clan) continue;
            treasury += clan->resources;
            net += EconomySystem::Get().Preview(world, clanId).net;
            tiles += CoverageSystem::Get().TilesOwnedBy(clanId);
            settlementCount += clan->settlements.size();
            cohortCount += clan->cohorts.size();

            for (EntityId settlementId : clan->settlements)
            {
                if (const Settlement* settlement = world.FindSettlement(settlementId))
                    population += settlement->population;
            }
            for (EntityId cohortId : clan->cohorts) soldiers += world.CohortStrength(cohortId);
        }

        const Rect view = area.Inset(theme.padding);
        f32 contentHeight = 300.0f + state->clans.size() * 26.0f + settlementCount * 24.0f;
        const Rect content = ui.BeginScroll(view, contentHeight, m_realmScroll);

        f32 y = content.y;
        auto row = [&]() { const Rect r{ content.x, y, content.w, theme.rowHeight }; y += theme.rowHeight + 2.0f; return r; };

        renderer.UIText(state->name, { content.x, y }, theme.textStrong, 1.25f);
        y += 28.0f;

        ui.KeyValue(row(), theme.Label("population"), std::to_string(population), theme.text);
        ui.KeyValue(row(), theme.Label("settlements"), std::to_string(settlementCount), theme.text);
        ui.KeyValue(row(), theme.Label("cohorts"), std::to_string(cohortCount), theme.text);
        ui.KeyValue(row(), "Воїнів", std::to_string(soldiers), theme.text);
        ui.KeyValue(row(), "Земель (клітин)", std::to_string(tiles), theme.text);

        y += 6.0f;
        ui.Label(row(), "СКАРБНИЦЯ", theme.accent);
        ui.KeyValue(row(), theme.Label("money"), FormatNumber(treasury.money) + "  (" + Signed(net.money) + ")",
                    net.money >= 0.0f ? theme.positive : theme.negative);
        ui.KeyValue(row(), theme.Label("food"), FormatNumber(treasury.food) + "  (" + Signed(net.food) + ")",
                    net.food >= 0.0f ? theme.positive : theme.negative);
        ui.KeyValue(row(), theme.Label("wood"), FormatNumber(treasury.wood) + "  (" + Signed(net.wood) + ")",
                    net.wood >= 0.0f ? theme.positive : theme.negative);
        ui.KeyValue(row(), theme.Label("stone"), FormatNumber(treasury.stone) + "  (" + Signed(net.stone) + ")",
                    net.stone >= 0.0f ? theme.positive : theme.negative);

        y += 6.0f;
        ui.Label(row(), theme.Label("clans"), theme.accent);
        for (EntityId clanId : state->clans)
        {
            const Clan* clan = world.FindClan(clanId);
            if (!clan) continue;
            const Rect r = row();
            ui.ListItem(r, clan->name, clanId == state->leader, clan->color);

            const Character* head = world.FindCharacter(clan->head);
            ui.LabelRight(r, head ? head->FullName() : "—", theme.textDim);
        }

        y += 6.0f;
        ui.Label(row(), theme.Label("settlements"), theme.accent);
        for (EntityId clanId : state->clans)
        {
            const Clan* clan = world.FindClan(clanId);
            if (!clan) continue;
            for (EntityId settlementId : clan->settlements)
            {
                Settlement* settlement = world.FindSettlement(settlementId);
                if (!settlement) continue;

                const Rect r{ content.x, y, content.w, 22.0f };
                y += 24.0f;
                if (ui.ListItem(r, settlement->name, settlementId == m_selected, clan->color))
                {
                    m_selectionKind = SelectionKind::Settlement;
                    m_selected = settlementId;
                    m_panelMode = PanelMode::Selection;
                    Renderer::Get().GetCamera().SetFocus(settlement->position);
                }
                ui.LabelRight(r, std::to_string(settlement->population), theme.textDim);
            }
        }

        ui.EndScroll();
    }

    void GameScene::DrawDiplomacyPanel(const Rect& area)
    {
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();
        DiplomacySystem& diplomacy = DiplomacySystem::Get();

        const State* self = world.HumanState();
        if (!self) return;

        const Rect view = area.Inset(theme.padding);
        f32 y = view.y;
        auto row = [&](f32 height)
        {
            const Rect r{ view.x, y, view.w, height };
            y += height + 3.0f;
            return r;
        };

        ui.Label(row(theme.rowHeight), "ДЕРЖАВИ", theme.accent);

        std::vector<EntityId> others;
        for (const auto& [id, state] : world.States())
        {
            if (id != self->id && !state.eliminated) others.push_back(id);
        }
        std::sort(others.begin(), others.end());

        for (EntityId id : others)
        {
            const State* other = world.FindState(id);
            if (!other) continue;

            const Rect r = row(26.0f);
            if (ui.ListItem(r, other->name, id == m_diplomacyTarget, other->color))
            {
                m_diplomacyTarget = id;
            }

            const DiplomaticStance stance = self->StanceWith(id);
            const Color stanceColor = stance == DiplomaticStance::War ? theme.negative
                                    : stance == DiplomaticStance::Alliance ? theme.positive
                                    : theme.textDim;
            ui.LabelRight(r, State::StanceName(stance), stanceColor);
        }

        if (m_diplomacyTarget == kInvalidId) return;
        const State* target = world.FindState(m_diplomacyTarget);
        if (!target) { m_diplomacyTarget = kInvalidId; return; }

        y += 8.0f;
        ui.Label(row(theme.rowHeight), "СТАВЛЕННЯ", theme.accent);

        const f32 opinion = diplomacy.Opinion(world, self->id, m_diplomacyTarget);
        {
            const Rect r = row(theme.rowHeight);
            ui.Label(r, target->name, theme.text);
            ui.LabelRight(r, Signed(opinion),
                          opinion >= 0.0f ? theme.positive : theme.negative);
        }
        for (const auto& [reason, value] : diplomacy.OpinionBreakdown(world, self->id, m_diplomacyTarget))
        {
            const Rect r = row(18.0f);
            ui.Label(r, "  " + reason, theme.textDim);
            ui.LabelRight(r, Signed(value), value >= 0.0f ? theme.positive : theme.negative);
        }

        y += 8.0f;
        ui.Label(row(theme.rowHeight), theme.Label("actions"), theme.accent);
        for (const DiplomaticAction& action : diplomacy.AvailableActions(world, self->id, m_diplomacyTarget))
        {
            const Rect r = row(26.0f);
            if (ui.Button(r, action.label, action.available))
            {
                if (diplomacy.Perform(world, self->id, m_diplomacyTarget, action.kind))
                {
                    m_status = action.label;
                    m_statusTimer = 3.0f;
                }
                else
                {
                    m_status = "Не вдалося";
                    m_statusTimer = 3.0f;
                }
            }
            ui.TooltipIfHovered(r, action.tooltip);
        }
    }

    void GameScene::DrawChroniclePanel(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        const std::vector<Chronicle>& entries = world.ChronicleEntries();
        const Rect view = area.Inset(theme.padding);
        const f32 rowHeight = renderer.TextHeight() + 6.0f;

        const Rect content = ui.BeginScroll(view, entries.size() * rowHeight, m_chronicleScroll);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const size_t index = entries.size() - 1 - i;   // newest first
            const Rect r{ content.x, content.y + i * rowHeight, content.w, rowHeight };
            renderer.UIText(entries[index].text, { r.x, r.y + 2.0f }, entries[index].color);
        }
        ui.EndScroll();
    }

    // =====================================================================================
    // Hover tooltip over the map
    // =====================================================================================

    void GameScene::DrawTooltipForHover()
    {
        UI& ui = UI::Get();
        World& world = World::Get();
        const RaceDatabase& races = RaceDatabase::Get();

        if (m_hoveredSettlement != kInvalidId)
        {
            const Settlement* settlement = world.FindSettlement(m_hoveredSettlement);
            if (!settlement) return;
            const Clan* owner = world.FindClan(settlement->owner);

            std::string text = settlement->name + " — " + settlement->TierName() + "\n";
            text += "Власник: " + (owner ? owner->name : std::string("незалежне")) + "\n";
            text += "Народ: " + races.Race(settlement->raceId).name +
                    ", віра: " + races.Faith(settlement->faithId).name + "\n";
            text += "Населення: " + std::to_string(settlement->population) +
                    ", вірність: " + Percent(settlement->loyalty);
            ui.Tooltip(text);
            return;
        }

        if (m_hoveredCohort != kInvalidId)
        {
            const Cohort* cohort = world.FindCohort(m_hoveredCohort);
            if (!cohort) return;
            const Clan* clan = world.FindClan(cohort->clan);

            std::string text = cohort->DisplayName() + "\n";
            text += (clan ? clan->name : std::string("—")) + "\n";
            text += "Людей: " + std::to_string(world.CohortStrength(cohort->id)) +
                    ", досвід: " + Percent(cohort->experience) + "\n";
            text += std::string("Наказ: ") + Task::TypeName(cohort->currentTask.type);
            ui.Tooltip(text);
        }
    }
}
