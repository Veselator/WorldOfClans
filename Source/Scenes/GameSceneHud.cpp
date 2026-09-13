// GameSceneHud.cpp - every panel of the in-game interface.
//
// Split out of GameScene.cpp so the scene file stays about the world and this one stays
// about presenting it. All of it is immediate-mode: what you see is what this frame drew.
#include "GameScene.h"
#include "../Net/NetSession.h"
#include "SceneManager.h"

#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Game/Factories/EvaluatorFactory.h"
#include "../Game/GameCommands.h"
#include "../Game/Systems/BanditSystem.h"
#include "../Game/Systems/BattleSystem.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/Systems/DiplomacySystem.h"
#include "../Game/Systems/DynastySystem.h"
#include "../Game/Systems/EconomySystem.h"
#include "../Game/Systems/ForestrySystem.h"
#include "../Game/Systems/MarketSystem.h"
#include "../Game/Systems/MovementSystem.h"
#include "../Game/Systems/PopulationSystem.h"
#include "../Game/Systems/RoadSystem.h"
#include "../Game/Systems/SettlementSystem.h"
#include "../Game/Systems/Simulation.h"
#include "../Game/World/RaceDatabase.h"
#include "../Game/World/World.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

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

        std::string Round(f32 value)
        {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%.0f", value);
            return buffer;
        }

        /// What a purse hands over, in the shortest honest form: "150 ср, 60 дер, 40 їжі".
        /// Whatever the order does not cost is simply not mentioned.
        std::string CostText(const ResourceData& cost)
        {
            std::string text;
            auto add = [&](f32 amount, const char* unit)
            {
                if (amount < 0.5f) return;
                if (!text.empty()) text += ", ";
                text += Round(amount) + " " + unit;
            };
            add(cost.money, "ср");
            add(cost.wood, "дер");
            add(cost.stone, "кам");
            add(cost.food, "їжі");
            return text.empty() ? "безкоштовно" : text;
        }

        /// The line that goes under a button: what it takes, then how long it takes.
        /// "150 ср, 60 дер | 60 дн." - one glance answers both questions a player has
        /// about anything he is about to order.
        std::string CostLine(const ResourceData& cost, i32 days)
        {
            const std::string when = days > 0 ? std::to_string(days) + " дн." : "миттєво";
            return CostText(cost) + "  |  " + when;
        }

        /// The same line, in pieces, each coloured by whether the treasury can actually
        /// meet it. A red button says "you cannot afford this"; a red *word* says which of
        /// the four things you are short of, which is the question the player is asking.
        std::vector<UI::CostPart> CostParts(const ResourceData& cost, const ResourceData& purse,
                                            i32 days, const Theme& theme, const char* unit = "дн.")
        {
            std::vector<UI::CostPart> parts;
            bool any = false;

            auto add = [&](f32 amount, f32 held, const char* unit)
            {
                if (amount < 0.5f) return;
                if (any) parts.push_back({ ", ", theme.textDim });
                parts.push_back({ Round(amount) + " " + unit,
                                  held >= amount ? theme.positive : theme.negative });
                any = true;
            };
            add(cost.money, purse.money, "ср");
            add(cost.wood, purse.wood, "дер");
            add(cost.stone, purse.stone, "кам");
            add(cost.food, purse.food, "їжі");

            if (!any) parts.push_back({ "безкоштовно", theme.positive });

            parts.push_back({ "  |  ", theme.textDim });
            parts.push_back({ days > 0 ? std::to_string(days) + " " + unit : std::string("миттєво"),
                              theme.textDim });
            return parts;
        }

        /// A figure for a tooltip: whole when it is whole, one decimal when it is not.
        std::string Num(f32 value)
        {
            char buffer[32];
            if (std::abs(value - std::round(value)) < 0.05f) std::snprintf(buffer, sizeof(buffer), "%.0f", value);
            else std::snprintf(buffer, sizeof(buffer), "%.1f", value);
            return buffer;
        }

        const char* ResourceUnit(ResourceType type)
        {
            switch (type)
            {
            case ResourceType::Money: return "срібла";
            case ResourceType::Wood:  return "дерева";
            case ResourceType::Stone: return "каменю";
            case ResourceType::Food:  return "їжі";
            default:                  return "";
            }
        }

        /// "x1.25" as "+25 %", "x0.8" as "-20 %".
        std::string PercentChange(f32 multiplier)
        {
            const i32 percent = static_cast<i32>(std::lround((multiplier - 1.0f) * 100.0f));
            return (percent >= 0 ? "+" : "") + std::to_string(percent) + " %";
        }

        /// What a building actually does, one effect per line, read straight off its data so
        /// the tooltip can never promise something the decorator does not deliver.
        std::string DescribeBuildingEffect(const BuildingInfo& b)
        {
            std::string out;
            auto line = [&](const std::string& text) { if (!out.empty()) out += "\n"; out += text; };

            if (b.decorator == DecoratorKind::Production)
            {
                const std::string unit = ResourceUnit(b.resource);
                if (std::abs(b.multiplier - 1.0f) > 0.001f)
                    line("Видобуток " + unit + ": " + PercentChange(b.multiplier));
                if (b.flat > 0.001f)
                    line("+" + Num(b.flat) + " " + unit + " на місяць");
            }
            if (std::abs(b.defenseMultiplier - 1.0f) > 0.001f) line("Оборона: " + PercentChange(b.defenseMultiplier));
            if (b.trainingBonus > 0.001f) line("Вишкіл: +" + std::to_string(static_cast<i32>(std::lround(b.trainingBonus * 100.0f))) + " %");
            if (std::abs(b.recruitCostMultiplier - 1.0f) > 0.001f) line("Ціна набору: " + PercentChange(b.recruitCostMultiplier));
            if (std::abs(b.cavalryCostMultiplier - 1.0f) > 0.001f) line("Ціна кінноти: " + PercentChange(b.cavalryCostMultiplier));
            if (b.moraleBonus > 0.001f) line("Бойовий дух: +" + std::to_string(static_cast<i32>(std::lround(b.moraleBonus * 100.0f))) + " %");
            if (std::abs(b.loyaltyPerMonth) > 0.0001f)
                line(std::string("Вірність: ") + (b.loyaltyPerMonth > 0.0f ? "+" : "") +
                     Num(b.loyaltyPerMonth * 100.0f) + " % на місяць");
            if (std::abs(b.conversionCostMultiplier - 1.0f) > 0.001f) line("Ціна навернення: " + PercentChange(b.conversionCostMultiplier));
            if (std::abs(b.distancePenaltyMultiplier - 1.0f) > 0.001f) line("Вплив відстані на вірність: " + PercentChange(b.distancePenaltyMultiplier));
            if (std::abs(b.coverageMultiplier - 1.0f) > 0.001f) line("Покриття: " + PercentChange(b.coverageMultiplier));
            if (b.caravanBonus > 0.001f) line("Торгівля: +" + std::to_string(static_cast<i32>(std::lround(b.caravanBonus * 100.0f))) + " % срібла (повністю — лише при дорозі)");
            if (b.forestHarvest > 0.001f) line("Вирубує " + Num(b.forestHarvest) + " лісу на місяць");
            if (b.fieldRadiusBonus > 0.001f) line("Поля розходяться далі: +" + Num(b.fieldRadiusBonus));
            if (b.siegeSupplyDays > 0.001f) line("Запаси на облогу: +" + Num(b.siegeSupplyDays) + " дн.");
            if (b.bridgesWater) line("Прокладає мости через воду поблизу");
            return out;
        }

        /// Monthly output a standing building is responsible for, as "+3.5 дер, +1 їжі".
        /// Empty when it brings in nothing, which is what the list shows for walls.
        std::string ContributionText(const ResourceData& gain)
        {
            std::string out;
            auto add = [&](f32 amount, const char* unit)
            {
                if (amount < 0.05f) return;
                if (!out.empty()) out += ", ";
                out += "+" + Num(amount) + " " + unit;
            };
            add(gain.money, "ср");
            add(gain.wood, "дер");
            add(gain.stone, "кам");
            add(gain.food, "їжі");
            return out;
        }
    }

    // =====================================================================================
    // Top bar
    // =====================================================================================

    void GameScene::DrawTopBar()
    {
        Simulation& simulation = Simulation::Get();

        const Vec2 viewport = m_renderer.ViewportSize();
        const Rect bar{ 0.0f, 0.0f, viewport.x, m_theme.topBarHeight };

        m_renderer.UIRect(bar, m_theme.panelHeader.WithAlpha(0.97f));
        m_renderer.UIRect({ 0.0f, bar.Bottom() - 1.0f, viewport.x, 1.0f }, m_theme.border);
        m_ui.BlockMouse(bar);

        f32 x = 12.0f;

        // --- realm ---------------------------------------------------------------------------
        const State* state = m_world.HumanState();
        Clan* clan = m_world.HumanClan();
        if (state)
        {
            m_renderer.UIRect({ x, 7.0f, 14.0f, 14.0f }, state->color);
            x += 20.0f;
            m_renderer.UIText(state->name, { x, (m_theme.topBarHeight - m_renderer.TextHeight()) * 0.5f },
                            m_theme.textStrong);
            x += m_renderer.TextWidth(state->name) + 24.0f;
        }

        // --- treasury -------------------------------------------------------------------------
        if (clan)
        {
            const ClanBudget budget = EconomySystem::Get().Preview(m_world, clan->id);
            struct Entry { const char* labelKey; f32 stock; f32 net; };
            const Entry entries[] = {
                { "money", clan->resources.money, budget.net.money },
                { "food",  clan->resources.food,  budget.net.food  },
                { "wood",  clan->resources.wood,  budget.net.wood  },
                { "stone", clan->resources.stone, budget.net.stone },
            };

            for (const Entry& entry : entries)
            {
                const std::string label = m_theme.Label(entry.labelKey);
                m_renderer.UIText(label + ":", { x, (m_theme.topBarHeight - m_renderer.TextHeight()) * 0.5f },
                                m_theme.textDim);
                x += m_renderer.TextWidth(label) + 10.0f;

                const std::string amount = FormatNumber(entry.stock);
                m_renderer.UIText(amount, { x, (m_theme.topBarHeight - m_renderer.TextHeight()) * 0.5f },
                                m_theme.textStrong);
                x += m_renderer.TextWidth(amount) + 6.0f;

                const std::string delta = "(" + Signed(entry.net) + ")";
                m_renderer.UIText(delta, { x, (m_theme.topBarHeight - m_renderer.TextHeight()) * 0.5f },
                                entry.net >= 0.0f ? m_theme.positive : m_theme.negative);
                x += m_renderer.TextWidth(delta) + 22.0f;
            }
        }

        // --- clock -----------------------------------------------------------------------------
        const std::string date = m_world.Time().ToString();
        const f32 speedWidth = 30.0f;
        const f32 speedCount = static_cast<f32>(simulation.SpeedSteps().size());
        const f32 speedBlock = speedWidth * speedCount + 8.0f;

        const f32 dateX = viewport.x - speedBlock - m_renderer.TextWidth(date) - 24.0f;
        m_renderer.UIText(date, { dateX, (m_theme.topBarHeight - m_renderer.TextHeight()) * 0.5f }, m_theme.accent);

        f32 speedX = viewport.x - speedBlock - 8.0f;
        for (size_t i = 0; i < simulation.SpeedSteps().size(); ++i)
        {
            const Rect button{ speedX, 3.0f, speedWidth - 3.0f, m_theme.topBarHeight - 6.0f };
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

            m_renderer.UIRect(button, active ? m_theme.accent.WithAlpha(0.35f) : m_theme.panel);
            m_renderer.UIRectOutline(button, active ? m_theme.accent : m_theme.border, 1.0f);
            m_renderer.UITextCentered(label, button, active ? m_theme.textStrong : m_theme.textDim);
            if (m_ui.InvisibleButton(button, "speed" + label) && NetSession::Get().MayControlSpeed())
                simulation.SetSpeedIndex(static_cast<i32>(i));
            speedX += speedWidth;
        }
    }

    // =====================================================================================
    // Chronicle strip and status line
    // =====================================================================================

    void GameScene::DrawChronicle()
    {
        const Vec2 viewport = m_renderer.ViewportSize();
        const std::vector<Chronicle>& entries = m_world.ChronicleEntries();

        const size_t shown = std::min<size_t>(entries.size(), 5);
        f32 y = viewport.y - m_theme.bottomBarHeight - 14.0f - shown * (m_renderer.TextHeight() + 3.0f);

        for (size_t i = entries.size() - shown; i < entries.size(); ++i)
        {
            // Older lines fade out rather than disappearing abruptly.
            const f32 age = static_cast<f32>(entries.size() - i) / static_cast<f32>(shown + 1);
            m_renderer.UIText(entries[i].text, { 12.0f, y }, entries[i].color.WithAlpha(1.0f - age * 0.55f));
            y += m_renderer.TextHeight() + 3.0f;
        }

        if (m_statusTimer > 0.0f && !m_status.empty())
        {
            const f32 width = m_renderer.TextWidth(m_status) + 24.0f;
            const Rect box{ (viewport.x - width) * 0.5f, viewport.y * 0.16f, width, 28.0f };
            m_renderer.UIRect(box, m_theme.panel.WithAlpha(0.9f * std::min(1.0f, m_statusTimer)));
            m_renderer.UIRectOutline(box, m_theme.accent.WithAlpha(std::min(1.0f, m_statusTimer)), 1.0f);
            m_renderer.UITextCentered(m_status, box, m_theme.textStrong.WithAlpha(std::min(1.0f, m_statusTimer)));
        }
    }

    // =====================================================================================
    // Bottom bar: the selected army's units
    // =====================================================================================

    void GameScene::DrawBottomBar()
    {
        if (m_selectionKind != SelectionKind::Cohort) return;


        Cohort* cohort = m_world.FindCohort(m_selected);
        if (!cohort) return;

        const Vec2 viewport = m_renderer.ViewportSize();
        const f32 lift = m_ui.SlideIn("game.bottom", true, m_theme.bottomBarHeight, 0.20f);
        const Rect bar{ 0.0f, viewport.y - m_theme.bottomBarHeight + lift,
                        viewport.x - m_theme.sidebarWidth - 8.0f, m_theme.bottomBarHeight };
        m_ui.Panel(bar);

        const Rect header{ bar.x + m_theme.padding, bar.y + 4.0f, bar.w - m_theme.padding * 2.0f, 18.0f };
        m_renderer.UIText(cohort->DisplayName() + "  —  " + Task::TypeName(cohort->currentTask.type),
                        { header.x, header.y }, m_theme.accent);

        const std::string summary = "Досвід " + Percent(cohort->experience) +
                                    "   Постачання " + Percent(cohort->supply) +
                                    "   Усього " + std::to_string(m_world.CohortStrength(cohort->id)) + " чол.";
        m_ui.LabelRight(header, summary, m_theme.textDim);

        // One card per unit; click to inspect its people.
        const f32 cardWidth = 118.0f;
        const f32 cardHeight = m_theme.bottomBarHeight - 32.0f;
        f32 x = bar.x + m_theme.padding;

        for (EntityId unitId : cohort->units)
        {
            Unit* unit = m_world.FindUnit(unitId);
            if (!unit) continue;
            if (x + cardWidth > bar.Right() - m_theme.padding) break;

            const Rect card{ x, bar.y + 26.0f, cardWidth - 6.0f, cardHeight };
            const bool selected = unitId == m_selectedUnit;

            m_renderer.UIRect(card, selected ? m_theme.panelAlt : m_theme.panel);
            m_renderer.UIRectOutline(card, selected ? m_theme.accent : m_theme.border, 1.0f);

            const RoleInfo& role = UnitDatabase::Get().Role(unit->role);
            m_renderer.UIText(role.name, { card.x + 6.0f, card.y + 5.0f }, m_theme.text);

            const std::string strength = std::to_string(unit->Strength()) + "/" +
                                         std::to_string(unit->establishment);
            m_renderer.UIText(strength, { card.x + 6.0f, card.y + 22.0f }, m_theme.textStrong);

            m_ui.ProgressBar({ card.x + 6.0f, card.y + 40.0f, card.w - 12.0f, 7.0f },
                           unit->StrengthFraction(), ValueColor(unit->StrengthFraction(), m_theme));
            m_ui.ProgressBar({ card.x + 6.0f, card.y + 50.0f, card.w - 12.0f, 7.0f },
                           unit->morale, m_theme.accent);

            if (m_ui.InvisibleButton(card, "unitcard" + std::to_string(unitId)))
            {
                m_selectedUnit = unitId;
                m_selectedCharacter = kInvalidId;
                m_panelMode = PanelMode::Selection;
            }
            m_ui.TooltipIfHovered(card, role.name + "\n" + role.description +
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
        const Vec2 viewport = m_renderer.ViewportSize();
        // The panel now stops where the minimap starts, rather than running to the floor.
        const Rect minimap = MinimapRect();
        const Rect panel{ viewport.x - m_theme.sidebarWidth, m_theme.topBarHeight,
                          m_theme.sidebarWidth, minimap.y - m_theme.topBarHeight };
        m_ui.Panel(panel);

        // --- tabs -------------------------------------------------------------------------
        const char* tabs[] = { "Вибір", "Держава", "Дипломатія", "Хроніка" };
        const f32 tabWidth = panel.w / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const Rect tab{ panel.x + i * tabWidth, panel.y, tabWidth, m_theme.headerHeight };
            const bool active = static_cast<int>(m_panelMode) == i;
            m_renderer.UIRect(tab, active ? m_theme.panelAlt : m_theme.panelHeader);
            m_renderer.UITextCentered(tabs[i], tab, active ? m_theme.accent : m_theme.textDim);

            // The key that gets here, written where the hand can find it.
            const std::string key = "^" + std::to_string(i + 1);
            m_renderer.UIText(key, { tab.x + 5.0f, tab.y + 3.0f },
                              active ? m_theme.accent.WithAlpha(0.8f) : m_theme.textDim.WithAlpha(0.6f),
                              0.8f);

            if (active) m_renderer.UIRect({ tab.x, tab.Bottom() - 2.0f, tab.w, 2.0f }, m_theme.accent);
            if (m_ui.InvisibleButton(tab, tabs[i])) m_panelMode = static_cast<PanelMode>(i);
            m_ui.TooltipIfHovered(tab, std::string(tabs[i]) + "\n\nКлавіші: Shift + " + std::to_string(i + 1));
        }

        // Whenever the panel changes subject, its content slides in from the right.
        const std::string key = std::to_string(static_cast<int>(m_panelMode)) + ":" +
                                std::to_string(static_cast<int>(m_selectionKind)) + ":" +
                                std::to_string(m_selected) + ":" + std::to_string(m_selectedUnit) +
                                ":" + std::to_string(m_selectedCharacter);
        if (key != m_panelKey)
        {
            m_panelKey = key;
            m_ui.RestartTransition("game.side");
        }
        const f32 slide = m_ui.SlideIn("game.side", true, 22.0f, 0.16f);

        const Rect body{ panel.x + slide, panel.y + m_theme.headerHeight,
                         panel.w, panel.h - m_theme.headerHeight };

        switch (m_panelMode)
        {
        case PanelMode::Realm:      DrawRealmPanel(body); return;
        case PanelMode::Diplomacy:  DrawDiplomacyPanel(body); return;
        case PanelMode::Chronicle:  DrawChroniclePanel(body); return;
        default: break;
        }

        if (m_selectionKind == SelectionKind::Settlement)
        {
            if (Settlement* settlement = m_world.FindSettlement(m_selected))
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
            if (Cohort* cohort = m_world.FindCohort(m_selected))
            {
                DrawCohortPanel(body, *cohort);
                return;
            }
            m_selectionKind = SelectionKind::None;
            m_selected = kInvalidId;
            m_selectedUnit = kInvalidId;
            m_selectedCharacter = kInvalidId;
        }
        else if (m_selectionKind == SelectionKind::Mine)
        {
            if (MineSite* mine = m_world.FindMine(m_selected))
            {
                DrawMinePanel(body, *mine);
                return;
            }
            m_selectionKind = SelectionKind::None;
            m_selected = kInvalidId;
        }
        else if (m_selectionKind == SelectionKind::BanditCamp)
        {
            if (BanditCamp* camp = m_world.FindBanditCamp(m_selected))
            {
                DrawBanditCampPanel(body, *camp);
                return;
            }
            // Burnt out while the panel was open.
            m_selectionKind = SelectionKind::None;
            m_selected = kInvalidId;
        }

        DrawConstructionPanel(body);
    }

    // =====================================================================================
    // Construction: what a realm raises on the map itself
    // =====================================================================================

    void GameScene::DrawConstructionPanel(const Rect& area)
    {
        const Rect view = area.Inset(m_theme.padding);

        // The list of what is under way can run past the bottom of the panel on a busy
        // realm, so the whole thing scrolls.
        const Rect content = m_ui.BeginScroll(view, view.h, m_sidePanelScroll);
        f32 y = content.y;

        auto row = [&](f32 height)
        {
            const Rect r{ content.x, y, content.w, height };
            y += height + 4.0f;
            return r;
        };

        m_ui.LabelCentered(row(22.0f), "Нічого не вибрано", m_theme.textDim);
        m_ui.LabelCentered(row(20.0f), "ЛКМ — вибрати, рамка — виділити", m_theme.textDim);

        Clan* clan = m_world.HumanClan();
        if (!clan) { m_ui.EndScroll(y); return; }

        y += 10.0f;
        m_ui.Label(row(20.0f), "БУДІВНИЦТВО", m_theme.accent);
        y += m_ui.Paragraph({ content.x, y, content.w, 0.0f },
                            "Тут зводять те, що стоїть на самій карті. Усе інше — казарми, "
                            "поля, стіни — це покращення поселень, і робиться в їхніх панелях.",
                            m_theme.textDim) + 10.0f;

        if (m_placingSettlement)
        {
            const SettlementKindInfo& info = SettlementDatabase::Get().Kind(m_placingKind);
            m_ui.Label(row(22.0f), "Оберіть місце: " + info.name, m_theme.warning);
            if (m_ui.Button(row(26.0f), "Скасувати")) m_placingSettlement = false;
            m_ui.EndScroll(y);
            return;
        }

        if (m_placingForest)
        {
            m_ui.Label(row(22.0f), "Оберіть, де саджати ліс", m_theme.warning);
            m_ui.Label(row(18.0f), "ПКМ або Esc — скасувати", m_theme.textDim);
            if (m_ui.Button(row(26.0f), "Скасувати")) m_placingForest = false;
            m_ui.EndScroll(y);
            return;
        }

        const SettlementDatabase& db = SettlementDatabase::Get();
        for (int i = 0; i < 3; ++i)
        {
            const SettlementKind kind = static_cast<SettlementKind>(i);
            const SettlementKindInfo& info = db.Kind(kind);

            const Rect r = row(36.0f);
            const bool affordable = clan->resources.CanAfford(info.buildCost);
            if (m_ui.CostButton(r, "Заснувати: " + info.name,
                                CostParts(info.buildCost, clan->resources, info.buildDays, m_theme),
                                true, affordable))
            {
                m_placingSettlement = true;
                m_placingForest = false;
                m_placingKind = kind;
                m_status = "Оберіть місце на карті";
                m_statusTimer = 4.0f;
            }

            m_ui.TooltipIfHovered(r,
                "Ціна: " + CostText(info.buildCost) + "\n"
                "Робота: " + std::to_string(info.buildDays) + " днів — поки її не скінчено, "
                "поселення нічого не дає й не тримає землі\n"
                "Переселенців: " + std::to_string(info.settlers) + " з довколишніх поселень\n"
                "Народ і віра — ваші власні." +
                std::string(affordable ? "" : "\n\nБракує коштів"));
        }

        // --- planting a wood --------------------------------------------------------------
        {
            const f32 radius = ConfigManager::Get().Float("forestry/plantRadius", 150.0f);

            // The panel cannot know where the player will put it, so the quoted price is for
            // a full stand on bare ground. What he actually pays is worked out from the spot
            // he picks - the less there is to plant there, the less it costs.
            ResourceData sample;
            i32 days = 0;
            ForestrySystem::PriceStand(kPi * radius * radius, sample, days);

            const Rect r = row(36.0f);
            std::vector<UI::CostPart> detail{ { "до ", m_theme.textDim } };
            for (const UI::CostPart& part : CostParts(sample, clan->resources, days, m_theme))
            {
                detail.push_back(part);
            }
            if (m_ui.CostButton(r, "Висадити ліс", detail, true,
                                clan->resources.CanAfford(sample * 0.3f)))
            {
                m_placingForest = true;
                m_placingSettlement = false;
                m_status = "Оберіть, де саджати";
                m_statusTimer = 4.0f;
            }
            m_ui.TooltipIfHovered(r,
                "Селяни засаджують ділянку молодняком. Ліс не з'являється одразу — "
                "йому потрібні роки, щоб піднятися.\n\n"
                "Платять лише за ту землю, де справді є що садити: за вже лісисту чи "
                "непридатну не беруть нічого.\n"
                "Садити можна лише неподалік своїх поселень.");
        }

        // --- what is already under way -------------------------------------------------------
        y += 10.0f;
        m_ui.Label(row(20.0f), "ПОТОЧНІ БУДІВНИЦТВА", m_theme.accent);

        bool anything = false;
        // Every row is a way to the site: a click takes the camera there and, where the work
        // belongs to a settlement, opens that settlement's panel on the page it concerns.
        auto work = [&](const std::string& what, const std::string& where, f32 progress,
                        const std::string& left, const Color& tint,
                        const Vec2* site = nullptr, EntityId town = kInvalidId,
                        SettlementTab page = SettlementTab::Overview)
        {
            anything = true;
            const Rect r = row(34.0f);
            if (site && m_ui.ListItem({ r.x - 3.0f, r.y - 2.0f, r.w + 6.0f, r.h + 2.0f }, "", false))
            {
                m_renderer.GetCamera().SetFocus(*site);
                if (town != kInvalidId)
                {
                    m_selectionKind = SelectionKind::Settlement;
                    m_selected = town;
                    m_selectedCohorts.clear();
                    m_settlementTab = page;
                    m_panelMode = PanelMode::Selection;
                    m_sidePanelScroll = 0.0f;
                }
            }
            if (site) m_ui.TooltipIfHovered(r, "Показати на карті");
            m_renderer.UIText(what, { r.x, r.y }, tint);
            m_ui.LabelRight({ r.x, r.y, r.w, 16.0f }, left, m_theme.textDim);
            m_renderer.UIText(where, { r.x, r.y + 15.0f }, m_theme.textDim, 0.82f);
            m_ui.ProgressBar({ r.x, r.Bottom() - 6.0f, r.w, 5.0f }, Clamp01(progress), tint);
        };

        // Seats being raised, and improvements going up inside the ones that stand.
        for (const auto& [id, settlement] : m_world.Settlements())
        {
            if (settlement.owner != clan->id) continue;

            if (settlement.UnderConstruction())
            {
                work(settlement.KindInfo().name, settlement.name, settlement.FoundingProgress(),
                     std::to_string(static_cast<i32>(settlement.foundingDaysLeft + 0.5f)) + " дн.",
                     m_theme.warning, &settlement.position, id, SettlementTab::Overview);
            }

            for (const ConstructionOrder& order : settlement.construction)
            {
                const BuildingInfo* info = BuildingDatabase::Get().Find(order.buildingId);
                const f32 total = info ? static_cast<f32>(std::max(1, info->buildDays)) : 1.0f;
                const f32 done = 1.0f - static_cast<f32>(order.daysRemaining) / total;
                work(info ? info->name : order.buildingId, settlement.name, done,
                     std::to_string(order.daysRemaining) + " дн.", m_theme.accent,
                     &settlement.position, id, SettlementTab::Buildings);
            }
        }

        // Roadworks.
        for (const RoadProject& project : RoadSystem::Get().Projects())
        {
            if (project.clan != clan->id) continue;
            const f32 left = std::max(0.0f, project.daysTotal - project.daysDone);
            // The road is shown at the point the work has reached.
            Vec2 roadhead;
            if (!project.tiles.empty())
            {
                const size_t at = std::min(project.stamped, project.tiles.size() - 1);
                const f32 tilePixels = static_cast<f32>(m_world.Map().TilePixels());
                roadhead = { (project.tiles[at].x + 0.5f) * tilePixels, (project.tiles[at].y + 0.5f) * tilePixels };
            }
            work("Шлях", project.label, project.daysDone / std::max(1.0f, project.daysTotal),
                 std::to_string(static_cast<i32>(left + 0.5f)) + " дн.", m_theme.textStrong,
                 project.tiles.empty() ? nullptr : &roadhead);
        }

        // Quarries being opened.
        for (const MineSite& mine : m_world.Mines())
        {
            if (mine.owner != clan->id || !mine.UnderWay()) continue;
            const Settlement* near = m_world.NearestSettlement(mine.position, 1e9f, clan->id);
            work("Каменярня", near ? "коло " + near->name : std::string("на карті"),
                 mine.Progress(),
                 std::to_string(static_cast<i32>(mine.daysRemaining + 0.5f)) + " дн.",
                 m_theme.textStrong, &mine.position);
        }

        // Woods being planted.
        for (const Plantation& stand : ForestrySystem::Get().Plantations())
        {
            if (stand.clan != clan->id) continue;
            const f32 left = std::max(0.0f, stand.daysTotal - stand.daysDone);
            work("Висадка лісу", stand.label, stand.Progress(),
                 std::to_string(static_cast<i32>(left + 0.5f)) + " дн.", Color::FromRGB(0x4D7A2D),
                 &stand.centre);
        }

        if (!anything) m_ui.Label(row(20.0f), "Нічого не будується", m_theme.textDim);

        m_ui.EndScroll(y);
    }

    void GameScene::DrawNamingDialog()
    {
        if (!m_namingOpen) return;

        const Vec2 viewport = m_renderer.ViewportSize();
        const f32 fade = m_ui.Transition("game.naming", true, 0.18f);

        m_renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, m_theme.shadow.WithAlpha(0.55f * fade));
        m_ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const Rect panel = NamingDialogRect();
        m_ui.Panel(panel);

        const SettlementKindInfo& info = SettlementDatabase::Get().Kind(m_placingKind);
        m_renderer.UITextCentered("Як наректи?", { panel.x, panel.y + 20.0f, panel.w, 24.0f },
                                  m_theme.accent, 1.3f);
        m_renderer.UITextCentered(info.name, { panel.x, panel.y + 48.0f, panel.w, 20.0f }, m_theme.textDim);

        m_ui.TextField({ panel.x + 30.0f, panel.y + 80.0f, panel.w - 60.0f, 28.0f },
                       "newSettlement", m_pendingName, 32);

        const f32 buttonWidth = 160.0f;
        const f32 buttonY = panel.Bottom() - 44.0f;

        if (m_ui.Button({ panel.Center().x - buttonWidth - 6.0f, buttonY, buttonWidth, 30.0f }, "Заснувати"))
        {
            const bool founded = Order(GameCommands::Found(m_placingKind, m_pendingSite, m_pendingName));
            if (founded || OrdersDeferred())
            {
                m_settlementTab = SettlementTab::Overview;
                m_status = "Поселення закладено";
            }
            else
            {
                m_status = "Заснувати не вдалося";
            }
            m_statusTimer = 3.0f;
            m_namingOpen = false;
            m_ui.RestartTransition("game.naming");
        }

        if (m_ui.Button({ panel.Center().x + 6.0f, buttonY, buttonWidth, 30.0f }, "Скасувати"))
        {
            m_namingOpen = false;
            m_ui.RestartTransition("game.naming");
        }
    }

    // =====================================================================================
    // Settlement panel
    // =====================================================================================

    void GameScene::DrawSettlementPanel(const Rect& area, Settlement& settlement)
    {
        const Clan* owner = m_world.FindClan(settlement.owner);
        const bool playerOwns = owner && m_world.HumanState() && owner->state == m_world.HumanState()->id;

        const Rect view = area.Inset(m_theme.padding);

        // --- the head of the panel never scrolls: name, and the way back to the overview -----
        f32 top = view.y;
        m_renderer.UISprite(settlement.Sprite(), { view.x, top, 32.0f, 32.0f },
                            owner ? owner->color : Color::FromRGB(0xB9C0C8));
        const f32 titleHeight = m_renderer.TextHeight(1.2f);
        m_renderer.UIText(settlement.name, { view.x + 40.0f, top + 2.0f }, m_theme.textStrong, 1.2f);
        m_renderer.UIText(settlement.TierName(), { view.x + 40.0f, top + 2.0f + titleHeight },
                          m_theme.textDim);
        top += titleHeight + m_renderer.TextHeight() + 8.0f;

        if (m_settlementTab != SettlementTab::Overview)
        {
            const Rect back{ view.x, top, 110.0f, 22.0f };
            if (m_ui.Button(back, "< Огляд")) m_settlementTab = SettlementTab::Overview;

            // One entry per tab, and the lookup is bounds-checked: adding a page and
            // forgetting its title should show an empty heading, not read past the end.
            static const char* kTitles[] = { "", "Покращення", "Набір до війська",
                                             "Прокласти шлях", "Додаткові дії", "Торг" };
            const size_t titleIndex = static_cast<size_t>(m_settlementTab);
            m_ui.LabelRight({ view.x, top, view.w, 22.0f },
                            titleIndex < std::size(kTitles) ? kTitles[titleIndex] : "",
                            m_theme.accent);
            top += 28.0f;
        }

        const Rect body{ view.x, top, view.w, view.Bottom() - top };

        // A first-frame estimate only: EndScroll below feeds back what the layout really used.
        const Rect content = m_ui.BeginScroll(body, 600.0f, m_sidePanelScroll);
        f32 y = content.y;

        switch (m_settlementTab)
        {
        case SettlementTab::Overview:  DrawSettlementOverview(content, settlement, y); break;
        case SettlementTab::Buildings: DrawSettlementBuildings(content, settlement, y); break;
        case SettlementTab::Recruit:   DrawSettlementRecruit(content, settlement, y); break;
        case SettlementTab::Roads:     DrawSettlementRoads(content, settlement, y); break;
        case SettlementTab::Actions:   DrawSettlementActions(content, settlement, y); break;
        case SettlementTab::Market:    DrawSettlementMarket(content, settlement, y); break;
        }

        // --- the way in to each page ---------------------------------------------------------
        if (playerOwns && settlement.UnderConstruction())
        {
            y += 10.0f;
            m_ui.Paragraph({ content.x, y, content.w, 0.0f },
                           "Поки не скінчено будівництво, тут нічого не звести й нікого не набрати.",
                           m_theme.textDim);
            y += 40.0f;
        }
        else if (playerOwns && m_settlementTab == SettlementTab::Overview)
        {
            y += 10.0f;
            struct Page { SettlementTab tab; const char* label; const char* key; const char* hint; };
            static const std::vector<Page> kAlways = {
                { SettlementTab::Buildings, "Покращення поселення", "Z",
                  "Що вже стоїть у місті й що ще можна звести." },
                { SettlementTab::Recruit,   "Набір до війська", "X",
                  "Набрати воїнів у гарнізон або у вибрану когорту." },
                { SettlementTab::Roads,     "Прокласти шлях", "C",
                  "Сполучити це поселення з сусіднім. Ціна — за метр." },
                { SettlementTab::Actions,   "Додаткові дії", "V",
                  "Віра, незалежність, спалення." },
            };

            // The market is a page only a town with one has.
            std::vector<Page> kPages = kAlways;
            if (MarketSystem::HasMarket(settlement))
            {
                kPages.push_back({ SettlementTab::Market, "Торг", "B",
                                   "Міняти одне добро на інше за нинішнім курсом." });
            }

            for (const Page& page : kPages)
            {
                const Rect r{ content.x, y, content.w, 28.0f };
                y += 32.0f;
                if (m_ui.Button(r, page.label))
                {
                    m_settlementTab = page.tab;
                    m_sidePanelScroll = 0.0f;
                }

                // The key sits in the corner of the button it stands for.
                m_renderer.UIText(page.key, { r.x + 8.0f, r.y + (r.h - m_renderer.TextHeight(0.85f)) * 0.5f },
                                  m_theme.textDim, 0.85f);
                m_ui.TooltipIfHovered(r, std::string(page.hint) + "\n\nКлавіша: " + page.key);
            }
        }

        m_ui.EndScroll(y);
    }

    // -------------------------------------------------------------------------------------
    // Overview: what the place *is*, and nothing a lord can click on
    // -------------------------------------------------------------------------------------

    void GameScene::DrawSettlementOverview(const Rect& content, Settlement& settlement, f32& y)
    {
        const RaceDatabase& races = RaceDatabase::Get();
        const Clan* owner = m_world.FindClan(settlement.owner);

        const f32 rowHeight = m_theme.rowHeight;
        auto row = [&]() { const Rect r{ content.x, y, content.w, rowHeight }; y += rowHeight + 2.0f; return r; };

        if (settlement.UnderConstruction())
        {
            const Rect banner = row();
            m_renderer.UIRect(banner, m_theme.warning.WithAlpha(0.18f));
            m_renderer.UITextCentered("БУДУЄТЬСЯ", banner, m_theme.warning);

            const Rect bar = row();
            m_ui.Label(bar, "Готовність", m_theme.textDim);
            m_ui.ProgressBar({ bar.x + 110.0f, bar.y + 5.0f, bar.w - 110.0f, 12.0f },
                             settlement.FoundingProgress(), m_theme.warning,
                             std::to_string(static_cast<i32>(settlement.foundingDaysLeft + 0.5f)) + " дн.");
            m_ui.TooltipIfHovered(bar,
                "Поки будують, поселення нічого не дає, не тримає землі\n"
                "й не приймає ні наказів, ні наборів.");
            y += 6.0f;
        }

        m_ui.KeyValue(row(), m_theme.Label("owner"), owner ? owner->name : "Незалежне",
                      owner ? owner->color : m_theme.textDim);
        m_ui.KeyValue(row(), m_theme.Label("race"), races.Race(settlement.raceId).name, m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("faith"), races.Faith(settlement.faithId).name,
                      races.Faith(settlement.faithId).color);
        m_ui.KeyValue(row(), m_theme.Label("population"), std::to_string(settlement.population), m_theme.text);

        // --- meters ----------------------------------------------------------------------------
        {
            const Rect r = row();
            const f32 fraction = settlement.ProsperityFraction();
            m_ui.Label(r, m_theme.Label("prosperity"), m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, fraction,
                             ValueColor(fraction, m_theme), FormatNumber(settlement.prosperity));
            m_ui.TooltipIfHovered(r, "Статок: " + FormatNumber(settlement.prosperity) + " з " +
                                     FormatNumber(settlement.ProsperityCeiling()) +
                                     ", які здатне прогодувати поселення цього розміру.");
        }
        {
            const Rect r = row();
            m_ui.Label(r, m_theme.Label("loyalty"), m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, settlement.loyalty,
                             ValueColor(settlement.loyalty, m_theme), Percent(settlement.loyalty));
            const f32 forecast = PopulationSystem::Get().LoyaltyForecast(m_world, settlement.id);
            m_ui.TooltipIfHovered(r, "Зміна за місяць: " + Signed(forecast * 100.0f) + " %");
        }

        // --- output ------------------------------------------------------------------------------
        y += 6.0f;
        m_ui.Label(row(), "ВИРОБНИЦТВО ЗА МІСЯЦЬ", m_theme.accent);
        const ResourceData output = EconomySystem::Get().SettlementOutput(m_world, settlement.id);
        m_ui.KeyValue(row(), m_theme.Label("food"), FormatNumber(output.food), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("money"), FormatNumber(output.money), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("wood"), FormatNumber(output.wood), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("stone"), FormatNumber(output.stone), m_theme.text);

        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(settlement, m_world.Map());
        m_ui.KeyValue(row(), m_theme.Label("coverage"), FormatNumber(evaluator->Coverage()), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("defense"), FormatNumber(
            BattleSystem::Get().SettlementDefense(m_world, settlement.id)), m_theme.text);

        if (settlement.besiegedBy != kInvalidId)
        {
            const Rect r = row();
            m_ui.Label(r, "ОБЛОГА", m_theme.negative);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f },
                             settlement.siegeProgress, m_theme.negative, Percent(settlement.siegeProgress));
        }
        if (settlement.conversionDaysLeft > 0)
        {
            m_ui.KeyValue(row(), "Навернення", std::to_string(settlement.conversionDaysLeft) + " дн.",
                          m_theme.warning);
        }

        // --- garrison -------------------------------------------------------------------------------
        y += 8.0f;
        m_ui.Label(row(), m_theme.Label("garrison"), m_theme.accent);
        bool anyGarrison = false;
        for (const auto& [cohortId, cohort] : m_world.Cohorts())
        {
            if (cohort.garrisonOf != settlement.id) continue;
            anyGarrison = true;

            const Rect r = row();
            if (m_ui.ListItem(r, cohort.DisplayName(), IsSelected(cohortId), ClanColor(cohort.clan)))
            {
                SelectCohort(cohortId, m_input.IsKeyDown(Key::Shift) || m_input.IsKeyDown(Key::Control));
            }
            m_ui.LabelRight(r, std::to_string(m_world.CohortStrength(cohortId)) + " чол.", m_theme.textDim);
        }
        if (!anyGarrison) m_ui.Label(row(), "Немає війська", m_theme.textDim);

        // Standing buildings belong on the overview; putting them up does not.
        y += 8.0f;
        m_ui.Label(row(), "БУДІВЛІ", m_theme.accent);
        if (settlement.buildings.empty() && settlement.construction.empty())
        {
            m_ui.Label(row(), "Нічого не збудовано", m_theme.textDim);
        }
        for (const std::string& buildingId : settlement.buildings)
        {
            const BuildingInfo* info = BuildingDatabase::Get().Find(buildingId);
            const Rect r = row();
            m_ui.Label(r, "- " + (info ? info->name : buildingId), m_theme.text);

            // What it is earning this month, as the settlement actually stands - the same sum
            // with and without it, so siege, roads and the other buildings are all counted.
            const std::string earns = ContributionText(
                EconomySystem::Get().BuildingContribution(m_world, settlement, buildingId));
            if (!earns.empty()) m_ui.LabelRight(r, earns, m_theme.positive);

            if (info) m_ui.TooltipIfHovered(r, info->description, DescribeBuildingEffect(*info), m_theme.positive);
        }
        for (const ConstructionOrder& order : settlement.construction)
        {
            const Rect r = row();
            const BuildingInfo* info = BuildingDatabase::Get().Find(order.buildingId);
            m_ui.Label(r, "* " + (info ? info->name : order.buildingId), m_theme.warning);
            m_ui.LabelRight(r, std::to_string(order.daysRemaining) + " дн.", m_theme.textDim);
        }
    }

    // -------------------------------------------------------------------------------------
    // Buildings
    // -------------------------------------------------------------------------------------

    void GameScene::DrawSettlementBuildings(const Rect& content, Settlement& settlement, f32& y)
    {
        SettlementSystem& settlements = SettlementSystem::Get();
        const Clan* owner = m_world.FindClan(settlement.owner);
        const f32 rowHeight = m_theme.rowHeight;
        auto row = [&]() { const Rect r{ content.x, y, content.w, rowHeight }; y += rowHeight + 2.0f; return r; };

        if (!settlement.construction.empty())
        {
            m_ui.Label(row(), "У РОБОТІ", m_theme.accent);
            for (const ConstructionOrder& order : settlement.construction)
            {
                const BuildingInfo* info = BuildingDatabase::Get().Find(order.buildingId);
                const Rect r = row();
                m_ui.Label(r, "* " + (info ? info->name : order.buildingId), m_theme.warning);
                m_ui.LabelRight({ r.x, r.y, r.w - 26.0f, r.h },
                                std::to_string(order.daysRemaining) + " дн.", m_theme.textDim);
                if (m_ui.Button({ r.Right() - 22.0f, r.y, 20.0f, r.h }, "x"))
                {
                    Order(GameCommands::CancelBuild(settlement.id, order.buildingId));
                    break;   // the queue has been rewritten under us
                }
            }
            y += 8.0f;
        }

        m_ui.Label(row(), "ЗВЕСТИ", m_theme.accent);
        const std::vector<BuildOption> options = settlements.BuildOptions(m_world, settlement.id);
        if (options.empty()) m_ui.Label(row(), "Більше нічого звести", m_theme.textDim);

        for (const BuildOption& option : options)
        {
            if (!option.building) continue;
            const Rect r{ content.x, y, content.w, 36.0f };
            y += 38.0f;

            const ResourceData purse = owner ? owner->resources : ResourceData{};
            if (m_ui.CostButton(r, option.building->name,
                                CostParts(option.building->cost, purse,
                                          option.building->buildDays, m_theme),
                                option.allowed, option.affordable))
            {
                Order(GameCommands::Build(settlement.id, option.building->id));
            }

            const ResourceData& cost = option.building->cost;
            std::string tooltip = option.building->description + "\n\nЦіна: " +
                FormatNumber(cost.money) + " срібла, " + FormatNumber(cost.wood) + " дерева, " +
                FormatNumber(cost.stone) + " каменю\nТермін: " +
                std::to_string(option.building->buildDays) + " дн.";
            if (!option.allowed && !option.blockedReason.empty()) tooltip += "\n\n" + option.blockedReason;
            else if (!option.affordable) tooltip += "\n\nБракує коштів";
            m_ui.TooltipIfHovered(r, tooltip, DescribeBuildingEffect(*option.building), m_theme.positive);
        }
    }

    // -------------------------------------------------------------------------------------
    // Recruitment
    // -------------------------------------------------------------------------------------

    void GameScene::DrawSettlementRecruit(const Rect& content, Settlement& settlement, f32& y)
    {
        SettlementSystem& settlements = SettlementSystem::Get();
        const f32 rowHeight = m_theme.rowHeight;
        auto row = [&]() { const Rect r{ content.x, y, content.w, rowHeight }; y += rowHeight + 2.0f; return r; };

        // Where the new men will go, said plainly before anything is paid for.
        EntityId destination = kInvalidId;
        if (m_selectionKind == SelectionKind::Cohort) destination = m_selected;
        if (destination == kInvalidId)
        {
            for (const auto& [id, cohort] : m_world.Cohorts())
            {
                if (cohort.garrisonOf == settlement.id) { destination = id; break; }
            }
        }
        const Cohort* target = m_world.FindCohort(destination);
        m_ui.KeyValue(row(), "Поповнити", target ? target->DisplayName() : "Нова когорта",
                      target ? m_theme.text : m_theme.accent);
        y += 6.0f;

        // What is being mustered. The town raises one company at a time; the rest wait.
        if (!settlement.recruitQueue.empty())
        {
            m_ui.Label(row(), "НАБИРАЄТЬСЯ", m_theme.accent);
            for (size_t i = 0; i < settlement.recruitQueue.size(); ++i)
            {
                const RecruitOrder& order = settlement.recruitQueue[i];
                const UnitData& raised = UnitDatabase::Get().Stats(settlement.raceId, order.role);
                const Rect r{ content.x, y, content.w, 30.0f };
                y += 32.0f;

                const bool active = i == 0;
                m_renderer.UIText(raised.name + "  (" + std::to_string(order.headCount) + ")",
                                  { r.x, r.y }, active ? m_theme.textStrong : m_theme.textDim);
                const std::string left = active
                    ? std::to_string(static_cast<i32>(std::ceil(std::max(0.0f, order.hoursLeft)))) + " год."
                    : "у черзі";
                m_ui.LabelRight({ r.x, r.y, r.w - 26.0f, 16.0f }, left, m_theme.textDim);
                if (active)
                {
                    m_ui.ProgressBar({ r.x, r.Bottom() - 8.0f, r.w - 26.0f, 5.0f }, order.Progress(), m_theme.accent);
                }
                const Rect cancel{ r.Right() - 22.0f, r.y, 20.0f, 20.0f };
                if (m_ui.Button(cancel, "x"))
                {
                    Order(GameCommands::CancelRecruit(settlement.id, static_cast<i32>(i)));
                    break;   // the queue has been rewritten under us
                }
                m_ui.TooltipIfHovered(cancel, "Скасувати: люди повернуться додому, половину срібла буде повернено.");
            }
            y += 6.0f;
        }

        // Men cost twice: once to raise and then every month they stay under the banner.
        // The second figure is the one that ruins a treasury, so it goes on the button too.
        const f32 upkeepScale = ConfigManager::Get().Float("economy/cohortUpkeepPerUnit", 0.04f) * 25.0f;

        // One key per kind of company, in the order the list shows them: the lord's own
        // retinue, then foot, bow, horse and horse-bow. Always with Shift - the bare letters
        // turn the settlement's pages, so a company can be ordered and the page left at once.
        struct RecruitKey { UnitRole role; Key key; const char* label; };
        static const RecruitKey kRecruitKeys[] = {
            { UnitRole::Aristocrat,  Key::Z, "Shift + Z" },
            { UnitRole::Swordsman,   Key::X, "Shift + X" },
            { UnitRole::Archer,      Key::C, "Shift + C" },
            { UnitRole::Cavalry,     Key::V, "Shift + V" },
            { UnitRole::HorseArcher, Key::B, "Shift + B" },
        };

        const std::vector<RecruitOption> options = settlements.RecruitOptions(m_world, settlement.id);
        for (const RecruitOption& option : options)
        {
            const Rect r{ content.x, y, content.w, 38.0f };
            y += 40.0f;

            const RecruitKey* shortcut = nullptr;
            for (const RecruitKey& entry : kRecruitKeys)
            {
                if (entry.role == option.role) { shortcut = &entry; break; }
            }
            const bool keyed = shortcut && !m_ui.WantsKeyboard() && m_input.IsKeyDown(Key::Shift) &&
                               m_input.WasKeyPressed(shortcut->key);

            const UnitData& stats = UnitDatabase::Get().Stats(settlement.raceId, option.role);
            const f32 upkeep = stats.upkeep * static_cast<f32>(option.headCount) * upkeepScale;

            ResourceData price;
            price.money = option.cost;

            const std::string label = option.name + "  (" + std::to_string(option.headCount) + ")";
            // The time on the button is the muster: how long before the company stands in
            // the square. Drill comes after, and is in the tooltip.
            const Clan* purseOwner = m_world.FindClan(settlement.owner);
            std::vector<UI::CostPart> detail = CostParts(
                price, purseOwner ? purseOwner->resources : ResourceData{},
                static_cast<i32>(stats.raiseHours + 0.5f), m_theme, "год.");
            detail.push_back({ "  ·  утримання " + Round(upkeep) + " ср/міс", m_theme.textDim });

            const bool clicked = m_ui.CostButton(r, label, detail,
                                                 option.blockedReason.empty() || option.affordable,
                                                 option.affordable);
            if (shortcut)
            {
                m_renderer.UIText(shortcut->label, { r.x + 8.0f, r.y + (r.h - m_renderer.TextHeight(0.85f)) * 0.5f },
                                  m_theme.textDim, 0.85f);
            }
            if (clicked || (keyed && option.affordable))
            {
                if (Order(GameCommands::Recruit(settlement.id, destination, option.role)) ||
                    OrdersDeferred())
                {
                    m_status = settlement.recruitQueue.size() > 1 ? "Загін поставлено в чергу"
                                                                   : "Загін набирається";
                    m_statusTimer = 2.5f;
                }
            }

            std::string tooltip = stats.name +
                "\nЗбір: " + Round(stats.raiseHours) + " год. (місто набирає по одному загону, решта чекає в черзі)" +
                "\nВишкіл: " + Round(stats.trainDays) + " дн. у гарнізоні (у полі — довше)" +
                "\nНабір: " + Round(option.cost) + " срібла" +
                "\nУтримання: " + Round(upkeep) + " срібла та " +
                Round(ConfigManager::Get().Float("economy/foodPerUnitPerMonth", 0.02f) *
                      static_cast<f32>(option.headCount)) + " їжі на місяць";
            if (!option.blockedReason.empty()) tooltip += "\n\n" + option.blockedReason;
            if (shortcut) tooltip += std::string("\n\nКлавіша: ") + shortcut->label;
            m_ui.TooltipIfHovered(r, tooltip);
        }
    }

    // -------------------------------------------------------------------------------------
    // Roads
    // -------------------------------------------------------------------------------------

    void GameScene::DrawSettlementRoads(const Rect& content, Settlement& settlement, f32& y)
    {
        const Clan* owner = m_world.FindClan(settlement.owner);
        if (!owner) return;

        RoadSystem& roads = RoadSystem::Get();
        const f32 rowHeight = m_theme.rowHeight;
        auto row = [&]() { const Rect r{ content.x, y, content.w, rowHeight }; y += rowHeight + 2.0f; return r; };

        // Paragraph returns what it actually used: a fixed advance guessed the line count
        // and the next heading landed on top of the last line.
        y += m_ui.Paragraph({ content.x, y, content.w, 0.0f },
                            "Шлях коштує за метр; там, де він перетинає ріку, сам собою "
                            "зводиться міст, і за його метри платять окремо.",
                            m_theme.textDim) + 10.0f;

        std::vector<std::pair<f32, EntityId>> candidates;
        for (EntityId otherId : owner->settlements)
        {
            if (otherId == settlement.id) continue;
            const Settlement* other = m_world.FindSettlement(otherId);
            if (!other) continue;
            if (roads.AreConnected(m_world, settlement.id, otherId)) continue;
            candidates.emplace_back(Distance(other->position, settlement.position), otherId);
        }
        std::sort(candidates.begin(), candidates.end());
        if (candidates.size() > 6) candidates.resize(6);

        m_ui.Label(row(), "КУДИ", m_theme.accent);
        if (candidates.empty()) m_ui.Label(row(), "Усі сусіди вже сполучені", m_theme.textDim);

        for (const auto& [distance, otherId] : candidates)
        {
            const Settlement* other = m_world.FindSettlement(otherId);
            if (!other) continue;

            const RoadPlan plan = roads.Plan(m_world, settlement.id, otherId);
            const Rect r{ content.x, y, content.w, 36.0f };
            y += 38.0f;

            const bool affordable = plan.valid && owner->resources.CanAfford(plan.cost);
            const std::string label = other->name + "  (" +
                FormatNumber(plan.metres / 1000.0f) + " км)";

            if (m_ui.CostButton(r, label, CostParts(plan.cost, owner->resources, plan.days, m_theme),
                                plan.valid, affordable))
            {
                Order(GameCommands::Road(settlement.id, otherId));
                m_status = "Шлях розпочато";
                m_statusTimer = 2.5f;
            }

            std::string tooltip = plan.valid
                ? "Довжина: " + FormatNumber(plan.metres) + " м\n"
                  "Мости: " + FormatNumber(plan.bridgeMetres) + " м\n"
                  "Ціна: " + FormatNumber(plan.cost.money) + " срібла, " +
                  FormatNumber(plan.cost.wood) + " дерева, " +
                  FormatNumber(plan.cost.stone) + " каменю\n"
                  "Термін: " + std::to_string(plan.days) + " дн."
                : plan.problem;
            m_ui.TooltipIfHovered(r, tooltip);
        }

        // Whatever is already under way, so the player can see his money at work.
        bool anyWork = false;
        for (const RoadProject& project : roads.Projects())
        {
            if (project.clan != owner->id) continue;
            if (!anyWork) { y += 8.0f; m_ui.Label(row(), "У РОБОТІ", m_theme.accent); anyWork = true; }

            const Rect r = row();
            m_ui.Label(r, "* " + project.label, m_theme.warning);
            m_ui.LabelRight(r, std::to_string(static_cast<i32>(
                (project.daysTotal - project.daysDone) + 0.5f)) + " дн.", m_theme.textDim);
        }
    }

    // -------------------------------------------------------------------------------------
    // Everything else a lord may do to a town of his own
    // -------------------------------------------------------------------------------------

    void GameScene::DrawSettlementActions(const Rect& content, Settlement& settlement, f32& y)
    {
        SettlementSystem& settlements = SettlementSystem::Get();
        const RaceDatabase& races = RaceDatabase::Get();
        const Clan* owner = m_world.FindClan(settlement.owner);
        if (!owner) return;

        const f32 rowHeight = m_theme.rowHeight;
        auto row = [&]() { const Rect r{ content.x, y, content.w, rowHeight }; y += rowHeight + 2.0f; return r; };

        // --- faith ---------------------------------------------------------------------------
        // A lord spreads his own faith and no other: there is nothing to be had from
        // pushing a town into a third god's hands.
        m_ui.Label(row(), "ВІРА", m_theme.accent);
        if (settlement.conversionDaysLeft > 0)
        {
            m_ui.KeyValue(row(), "Навернення триває",
                          std::to_string(settlement.conversionDaysLeft) + " дн.", m_theme.warning);
        }
        else if (settlement.faithId == owner->faithId)
        {
            m_ui.Label(row(), "Місто вже вашої віри", m_theme.textDim);
        }
        else
        {
            const FaithInfo& faith = races.Faith(owner->faithId);
            const Rect r{ content.x, y, content.w, 36.0f };
            y += 38.0f;

            const f32 cost = settlements.ConversionCost(m_world, settlement.id, owner->faithId);
            ResourceData price;
            price.money = cost;
            if (m_ui.CostButton(r, "Навернути до віри " + faith.name,
                                CostParts(price, owner->resources,
                                          RaceDatabase::Get().ConversionDays(), m_theme),
                                true, owner->resources.money >= cost))
            {
                Order(GameCommands::Convert(settlement.id, owner->faithId));
            }
            m_ui.TooltipIfHovered(r, "Перевести мешканців у віру свого роду. "
                                     "Ціна залежить від людності та завзяття громади.");
        }

        // --- the hard choices -------------------------------------------------------------------
        y += 12.0f;
        m_ui.Label(row(), "ВАЖКІ РІШЕННЯ", m_theme.accent);

        {
            const Rect r{ content.x, y, content.w, 26.0f };
            y += 30.0f;
            if (m_ui.Button(r, "Відпустити на волю"))
            {
                Order(GameCommands::Independence(settlement.id));
                m_settlementTab = SettlementTab::Overview;
            }
            m_ui.TooltipIfHovered(r, "Поселення більше не платить данини, зате й не бунтує.");
        }
        {
            const Rect r{ content.x, y, content.w, 26.0f };
            y += 30.0f;
            const bool allowed = settlement.kind == SettlementKind::Village;
            if (m_ui.Button(r, "Спалити дотла", allowed))
            {
                Order(GameCommands::Raze(settlement.id));
                m_selectionKind = SelectionKind::None;
                m_selected = kInvalidId;
                m_settlementTab = SettlementTab::Overview;
                return;
            }
            m_ui.TooltipIfHovered(r, allowed ? "Село зникає з карти назавжди."
                                             : "Спалити можна лише село.");
        }
    }

    void GameScene::DrawSettlementMarket(const Rect& content, Settlement& settlement, f32& y)
    {
        MarketSystem& market = MarketSystem::Get();
        Clan* clan = m_world.FindClan(settlement.owner);
        if (!clan) return;

        auto row = [&](f32 height)
        {
            const Rect r{ content.x, y, content.w, height };
            y += height + 3.0f;
            return r;
        };

        static const ResourceType kGoods[] = {
            ResourceType::Money, ResourceType::Wood, ResourceType::Stone, ResourceType::Food
        };

        // --- what things are worth today ------------------------------------------------------
        m_ui.Label(row(20.0f), "КУРСИ", m_theme.accent);
        y += m_ui.Paragraph({ content.x, y, content.w, 0.0f },
                            "Ціни в сріблі за одиницю. Їх рухає те, чого бракує або чого "
                            "забагато в державах, які теж мають торжище.", m_theme.textDim) + 6.0f;

        for (ResourceType good : kGoods)
        {
            if (good == ResourceType::Money) continue;

            const Rect r = row(22.0f);
            const MarketSystem::Quote quote = market.QuoteAt(m_world, settlement.id, good);
            const f32 trend = market.Trend(good);

            m_ui.Label(r, MarketSystem::ResourceName(good), m_theme.text);

            // An arrow for which way it has been going, and the two sides of the counter.
            const char* arrow = trend > 0.005f ? "^" : (trend < -0.005f ? "v" : "-");
            const Color trendColor = trend > 0.005f ? m_theme.negative
                                   : (trend < -0.005f ? m_theme.positive : m_theme.textDim);
            m_renderer.UIText(arrow, { r.x + 92.0f, r.y + 3.0f }, trendColor);

            m_ui.LabelRight({ r.x, r.y, r.w - 4.0f, r.h },
                            FormatNumber(quote.sell) + " / " + FormatNumber(quote.buy),
                            m_theme.textStrong);
            m_ui.TooltipIfHovered(r,
                std::string(MarketSystem::ResourceName(good)) + "\n"
                "Купці дадуть за одиницю: " + FormatNumber(quote.sell) + " срібла.\n"
                "Візьмуть за одиницю: " + FormatNumber(quote.buy) + " срібла.\n"
                "Їхній зиск: " + FormatNumber(quote.spread * 100.0f) + "%\n"
                "Попит на ринку: " + FormatNumber(market.Pressure(good)));
        }

        // --- the counter -------------------------------------------------------------------
        y += 10.0f;
        m_ui.Label(row(20.0f), "ОБМІН", m_theme.accent);

        auto picker = [&](const char* caption, ResourceType& chosen, ResourceType other)
        {
            m_ui.Label(row(18.0f), caption, m_theme.textDim);
            const Rect strip = row(26.0f);
            const f32 cell = strip.w / 4.0f;
            for (int i = 0; i < 4; ++i)
            {
                const ResourceType good = kGoods[i];
                const Rect r{ strip.x + i * cell, strip.y, cell - 3.0f, strip.h };
                const bool active = good == chosen;
                // The same good on both sides is not a trade, so it cannot be picked twice.
                if (m_ui.Button(r, MarketSystem::ResourceName(good), good != other) && good != other)
                {
                    chosen = good;
                }
                if (active) m_renderer.UIRectOutline(r, m_theme.accent, 1.0f);
            }
        };

        picker("Віддаємо", m_tradeGive, m_tradeTake);
        picker("Отримуємо", m_tradeTake, m_tradeGive);

        const f32 have = clan->resources[m_tradeGive];
        m_tradeAmount = std::min(m_tradeAmount, std::max(0.0f, have));

        // --- how much --------------------------------------------------------------------
        {
            m_ui.Label(row(18.0f), "Скільки", m_theme.textDim);
            const Rect strip = row(26.0f);
            const f32 cell = strip.w / 5.0f;
            const f32 steps[4] = { 10.0f, 50.0f, 100.0f, 250.0f };
            for (int i = 0; i < 4; ++i)
            {
                const Rect r{ strip.x + i * cell, strip.y, cell - 3.0f, strip.h };
                if (m_ui.Button(r, FormatNumber(steps[i]), have >= steps[i]))
                {
                    m_tradeAmount = steps[i];
                }
            }
            const Rect all{ strip.x + 4 * cell, strip.y, cell - 3.0f, strip.h };
            if (m_ui.Button(all, "Усе", have > 0.0f)) m_tradeAmount = std::floor(have);
        }

        const f32 amount = std::min(m_tradeAmount, have);
        const f32 gain = market.Preview(m_world, settlement.id, m_tradeGive, m_tradeTake, amount);

        m_ui.KeyValue(row(20.0f), "У скарбниці", FormatNumber(have), m_theme.text);
        m_ui.KeyValue(row(20.0f), "Віддамо", FormatNumber(amount) + " " +
                      MarketSystem::ResourceName(m_tradeGive), m_theme.text);
        m_ui.KeyValue(row(20.0f), "Дістанемо", FormatNumber(gain) + " " +
                      MarketSystem::ResourceName(m_tradeTake),
                      gain > 0.0f ? m_theme.positive : m_theme.textDim);

        y += 6.0f;
        const Rect deal = row(30.0f);
        const bool possible = amount > 0.0f && gain > 0.0f && m_tradeGive != m_tradeTake;
        if (m_ui.Button(deal, "Обміняти", possible))
        {
            if (Order(GameCommands::Trade(settlement.id, static_cast<i32>(m_tradeGive),
                                          static_cast<i32>(m_tradeTake), amount)) ||
                OrdersDeferred())
            {
                m_status = "Обміняно на " + FormatNumber(gain) + " " +
                           MarketSystem::ResourceName(m_tradeTake);
                m_statusTimer = 3.0f;
            }
        }
        m_ui.TooltipIfHovered(deal,
            "Великий торг сам зрушує ціну: що продаєш - дешевшає, що купуєш - дорожчає.\n"
            "Тож вигідніше міняти потроху й у багатому місті, де купцям є з ким змагатися.");
    }

    // =====================================================================================
    // Quarries
    // =====================================================================================

    void GameScene::DrawMinePanel(const Rect& area, MineSite& mine)
    {
        SettlementSystem& settlements = SettlementSystem::Get();
        const Clan* owner = m_world.FindClan(mine.owner);
        Clan* player = m_world.HumanClan();

        const Rect view = area.Inset(m_theme.padding);
        f32 y = view.y;
        auto row = [&](f32 height = 0.0f)
        {
            const f32 h = height > 0.0f ? height : m_theme.rowHeight;
            const Rect r{ view.x, y, view.w, h };
            y += h + 2.0f;
            return r;
        };

        m_renderer.UISprite(SpriteId::Quarry, { view.x, y, 32.0f, 32.0f },
                            owner ? owner->color : Color::FromRGB(0x9AA3AB));
        const f32 mineTitle = m_renderer.TextHeight(1.2f);
        m_renderer.UIText("Каменярня", { view.x + 40.0f, y + 2.0f }, m_theme.textStrong, 1.2f);
        m_renderer.UIText(mine.developed ? "Працює" : (mine.UnderWay() ? "Закладається" : "Не освоєна"),
                          { view.x + 40.0f, y + 2.0f + mineTitle }, m_theme.textDim);
        y += mineTitle + m_renderer.TextHeight() + 8.0f;

        m_ui.KeyValue(row(), m_theme.Label("owner"), owner ? owner->name : "Нічия",
                      owner ? owner->color : m_theme.textDim);
        m_ui.KeyValue(row(), "Багатство", FormatNumber(mine.richness), m_theme.text);

        const EntityId clanId = player ? player->id : kInvalidId;
        const SettlementSystem::MineOffer offer = settlements.MineOptions(m_world, clanId, mine.id);
        m_ui.KeyValue(row(), "Каменю за місяць", FormatNumber(offer.stonePerMonth), m_theme.text);

        y += 10.0f;
        if (mine.developed)
        {
            m_ui.Paragraph({ view.x, y, view.w, 0.0f },
                           owner && owner->id == clanId
                               ? "Каменярня працює на вас і щомісяця дає камінь."
                               : "Каменярню вже освоїв інший рід.", m_theme.textDim);
            return;
        }

        // Opening a quarry is the same work as raising one inside a town, so it takes the
        // same time - and until it is dug it shows how far along it is, like any building.
        if (mine.UnderWay())
        {
            const Rect bar = row(18.0f);
            m_ui.Label(bar, "Робота", m_theme.textDim);
            m_ui.ProgressBar({ bar.x + 110.0f, bar.y + 3.0f, bar.w - 110.0f, 12.0f },
                             mine.Progress(), m_theme.accent,
                             std::to_string(static_cast<i32>(mine.daysRemaining + 0.5f)) + " дн.");
            y += 8.0f;
            m_ui.Paragraph({ view.x, y, view.w, 0.0f },
                           owner && owner->id == clanId
                               ? "Ваші люди вже б'ють камінь. Треба дати їм час."
                               : "Каменярню закладає інший рід.", m_theme.textDim);
            return;
        }

        const Rect button = row(38.0f);
        if (m_ui.CostButton(button, "Освоїти каменярню",
                            CostParts(offer.cost, player ? player->resources : ResourceData{},
                                      offer.days, m_theme),
                            offer.allowed, offer.affordable))
        {
            if (Order(GameCommands::DevelopMine(mine.id)) || OrdersDeferred())
            {
                m_status = "Каменярню закладено";
                m_statusTimer = 2.5f;
            }
        }
        m_ui.TooltipIfHovered(button,
            "Ціна: " + FormatNumber(offer.cost.money) + " срібла, " +
            FormatNumber(offer.cost.wood) + " дерева\n\nРобота: " + std::to_string(offer.days) + " днів" +
            (offer.blockedReason.empty() ? std::string() : "\n\n" + offer.blockedReason));
    }

    void GameScene::DrawBanditCampPanel(const Rect& area, BanditCamp& camp)
    {
        const RaceDatabase& races = RaceDatabase::Get();
        const Clan* band = m_world.FindClan(camp.clan);

        const Rect view = area.Inset(m_theme.padding);
        f32 y = view.y;
        auto row = [&](f32 height = 0.0f)
        {
            const f32 h = height > 0.0f ? height : m_theme.rowHeight;
            const Rect r{ view.x, y, view.w, h };
            y += h + 2.0f;
            return r;
        };

        m_renderer.UISprite(SpriteId::BanditCamp, { view.x, y, 32.0f, 32.0f },
                            Color::FromRGB(0x8A8A8A));
        const f32 titleHeight = m_renderer.TextHeight(1.2f);
        m_renderer.UIText("Розбійницький табір", { view.x + 40.0f, y + 2.0f }, m_theme.textStrong, 1.2f);
        m_renderer.UIText(band ? band->name : "Невідома ватага",
                          { view.x + 40.0f, y + 2.0f + titleHeight }, m_theme.textDim);
        y += titleHeight + m_renderer.TextHeight() + 8.0f;

        m_ui.KeyValue(row(), m_theme.Label("race"), races.Race(camp.raceId).name, m_theme.text);

        {
            const Rect r = row();
            m_ui.Label(r, "Цілість", m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, camp.Health(),
                             camp.Health() > 0.5f ? m_theme.negative : m_theme.warning,
                             Percent(camp.Health()));
        }

        // How many of them are out on the roads at the moment, which is the figure that
        // actually matters to a lord deciding whether to ride out.
        i32 bands = 0;
        u32 heads = 0;
        for (const auto& [cohortId, cohort] : m_world.Cohorts())
        {
            if (cohort.homeCamp != camp.id || cohort.IsEmpty()) continue;
            ++bands;
            heads += m_world.CohortStrength(cohortId);
        }
        m_ui.KeyValue(row(), "Ватаг у полі", std::to_string(bands), m_theme.text);
        if (heads > 0) m_ui.KeyValue(row(), "Людей у них", std::to_string(heads), m_theme.text);

        y += 8.0f;
        m_ui.Label(row(), "ПІД ПІДЛОГОЮ", m_theme.accent);
        const ResourceData spoils = BanditSystem::Spoils(camp);
        m_ui.KeyValue(row(), m_theme.Label("money"), FormatNumber(spoils.money), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("food"), FormatNumber(spoils.food), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("wood"), FormatNumber(spoils.wood), m_theme.text);
        if (spoils.stone > 0.5f)
        {
            m_ui.KeyValue(row(), m_theme.Label("stone"), FormatNumber(spoils.stone), m_theme.text);
        }

        y += 10.0f;
        y += m_ui.Paragraph({ view.x, y, view.w, 0.0f },
                            "Табору не беруть в облогу й не займають — його палять. "
                            "Пошліть військо (ПКМ по табору), і все, що там закопано, "
                            "поїде до вашої скарбниці.", m_theme.textDim) + 8.0f;

        // The order is given from the map, but saying so here saves a player hunting for it.
        std::vector<Cohort*> armies = CommandableSelection();
        if (armies.empty())
        {
            m_ui.Label(row(), "Виберіть військо, щоб віддати наказ", m_theme.textDim);
            return;
        }

        const Rect storm = row(30.0f);
        if (m_ui.HighlightButton(storm, "Спалити табір (" + std::to_string(armies.size()) + ")",
                                 m_theme.negative))
        {
            i32 ordered = 0;
            for (Cohort* army : armies)
            {
                if (Order(GameCommands::Task(army->id, TaskType::Storm, camp.position, camp.id)) ||
                    OrdersDeferred())
                {
                    ++ordered;
                }
            }
            m_status = ordered > 0 ? "Військо йде палити табір" : "Туди не пройти";
            m_statusTimer = 3.0f;
        }
    }

    // =====================================================================================
    // Cohort, unit and character panels
    // =====================================================================================

    f32 GameScene::SlowestUnitSpeed(const Cohort& cohort) const
    {
        f32 slowest = 0.0f;
        for (EntityId unitId : cohort.units)
        {
            const Unit* unit = m_world.FindUnit(unitId);
            if (!unit) continue;
            const f32 speed = unit->Stats().speed;
            slowest = slowest <= 0.0f ? speed : std::min(slowest, speed);
        }
        return slowest;
    }

    void GameScene::DrawCohortPanel(const Rect& area, Cohort& cohort)
    {
        // A selected unit takes over the panel to show its people.
        if (m_selectedUnit != kInvalidId)
        {
            if (Unit* unit = m_world.FindUnit(m_selectedUnit))
            {
                DrawUnitDetails(area, *unit);
                return;
            }
            m_selectedUnit = kInvalidId;
        }

        const Clan* clan = m_world.FindClan(cohort.clan);
        const Rect area_ = area.Inset(m_theme.padding);

        // The panel scrolls. It used to run straight down the sidebar and simply stop at the
        // bottom edge, which was tolerable until the splitting list was unfolded: everything
        // below it - "split off the marked", "or simply in two", the garrison and disband
        // buttons - was then off the end of the world with no way to reach it.
        const Rect view = m_ui.BeginScroll(area_, 900.0f, m_sidePanelScroll);

        f32 y = view.y;
        auto row = [&](f32 height = 0.0f)
        {
            const f32 h = height > 0.0f ? height : m_theme.rowHeight;
            const Rect r{ view.x, y, view.w, h };
            y += h + 2.0f;
            return r;
        };

        m_renderer.UISprite(SpriteId::Cohort, { view.x, y, 32.0f, 32.0f },
                          clan ? clan->color : m_theme.textDim);
        const f32 hostTitle = m_renderer.TextHeight(1.2f);
        m_renderer.UIText(cohort.DisplayName(), { view.x + 40.0f, y + 2.0f }, m_theme.textStrong, 1.2f);
        m_renderer.UIText(clan ? clan->name : "—", { view.x + 40.0f, y + 2.0f + hostTitle },
                          m_theme.textDim);
        y += hostTitle + m_renderer.TextHeight() + 8.0f;

        // A host in contact says so before it says anything else.
        const BattleReport* battle = BattleSystem::Get().BattleOf(cohort.id);
        if (battle)
        {
            const bool weAttack = battle->attacker.cohort == cohort.id;
            const BattleSide& us = weAttack ? battle->attacker : battle->defender;
            const BattleSide& them = weAttack ? battle->defender : battle->attacker;

            const Rect banner = row(22.0f);
            m_renderer.UIRect(banner, m_theme.negative.WithAlpha(0.22f));
            m_renderer.UITextCentered("У БОЮ проти " + them.name, banner, m_theme.negative);

            m_ui.KeyValue(row(), "Триває", std::to_string(static_cast<i32>(battle->elapsedDays + 0.5f)) + " дн.",
                          m_theme.text);
            m_ui.KeyValue(row(), "Наші втрати",
                          std::to_string(us.losses) + " / " + std::to_string(us.hurt) + " пор.",
                          m_theme.text);
            m_ui.KeyValue(row(), "Втрати ворога",
                          std::to_string(them.losses) + " / " + std::to_string(them.hurt) + " пор.",
                          m_theme.text);
        }
        else if (cohort.IsWithdrawing())
        {
            const Rect banner = row(22.0f);
            m_renderer.UIRect(banner, m_theme.warning.WithAlpha(0.20f));
            m_renderer.UITextCentered("Відходить (" +
                std::to_string(static_cast<i32>(cohort.disengageDays + 0.5f)) + " дн.)",
                banner, m_theme.warning);
        }

        m_ui.KeyValue(row(), "Наказ", Task::TypeName(cohort.currentTask.type), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("units"), std::to_string(cohort.units.size()) + " / " +
                    std::to_string(UnitDatabase::Get().MaxUnitsPerCohort()), m_theme.text);
        m_ui.KeyValue(row(), "Усього людей", std::to_string(m_world.CohortStrength(cohort.id)), m_theme.text);

        u32 wounded = 0;
        for (EntityId unitId : cohort.units)
        {
            if (const Unit* unit = m_world.FindUnit(unitId)) wounded += unit->Wounded();
        }
        if (wounded > 0)
        {
            const Rect r = row();
            m_ui.KeyValue(r, "Поранених", std::to_string(wounded), m_theme.warning);
            m_ui.TooltipIfHovered(r,
                "Винесені з поля. Вони повертаються до лав із часом -\n"
                "швидше в гарнізоні, повільніше в поході, і зовсім кепсько надголодь.");
        }

        {
            const Rect r = row();
            m_ui.Label(r, m_theme.Label("experience"), m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, cohort.experience,
                           m_theme.accent, Percent(cohort.experience));
        }
        {
            const Rect r = row();
            m_ui.Label(r, "Постачання", m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, cohort.supply,
                           ValueColor(cohort.supply, m_theme), Percent(cohort.supply));
        }
        {
            const Rect r = row();
            m_ui.Label(r, "Лад", m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, cohort.organisation,
                           ValueColor(cohort.organisation, m_theme), Percent(cohort.organisation));
            m_ui.TooltipIfHovered(r,
                "Лад війська: чи тримає воно шик і чи доходять накази.\n"
                "Бій і похід його ламають, стоянка на своїй землі повертає.\n"
                "Чим більше військо, тим повільніше впорядковується.");
        }

        const f32 power = m_world.CohortPower(cohort.id);
        m_ui.KeyValue(row(), "Бойова сила", FormatNumber(power), m_theme.text);

        {
            MovementSystem& movement = MovementSystem::Get();
            const Rect r = row();
            const f32 pace = movement.CurrentPace(m_world, cohort.id);
            m_ui.KeyValue(r, "Хід за день", FormatNumber(pace), m_theme.text);
            m_ui.TooltipIfHovered(r,
                "Колона йде в ногу з найповільнішим підрозділом.\n"
                "Пагорби й ліс сповільнюють, дорога пришвидшує, "
                "кепське постачання втомлює.");
        }

        if (cohort.garrisonOf != kInvalidId)
        {
            if (const Settlement* home = m_world.FindSettlement(cohort.garrisonOf))
            {
                m_ui.KeyValue(row(), "У гарнізоні", home->name, m_theme.positive);
            }
        }

        y += 8.0f;
        m_ui.Label(row(), m_theme.Label("units"), m_theme.accent);

        // The slowest of these is what the whole column marches at, so each one's own pace
        // is worth reading: it is the answer to "what is holding us up".
        const f32 slowest = SlowestUnitSpeed(cohort);
        const f32 baseSpeed = ConfigManager::Get().Float("movement/baseSpeed", 14.0f);
        const f32 supplyFactor = 0.6f + Clamp01(cohort.supply) * 0.4f;

        for (EntityId unitId : cohort.units)
        {
            Unit* unit = m_world.FindUnit(unitId);
            if (!unit) continue;

            const Rect r = row(44.0f);
            const RoleInfo& role = UnitDatabase::Get().Role(unit->role);
            // Empty label, written on by hand: the row carries two lines of its own.
            if (m_ui.ListItem(r, "", unitId == m_selectedUnit))
            {
                m_selectedUnit = unitId;
                m_selectedCharacter = kInvalidId;
            }
            m_renderer.UIText(role.name, { r.x + 10.0f, r.y + 4.0f }, m_theme.textStrong);

            // The company's mark in the right-hand corner, in its own white: on the map the
            // marks wear the owner's colours, in the panel they only have to say what it is.
            const Vec4& mark = UnitIconPixels(unit->role);
            const f32 markHeight = 13.6f;   // two sheet pixels per pixel, less 15 %
            const f32 markPixel = markHeight / 8.0f;
            const f32 markWidth = mark.z * markPixel;
            const Rect markRect{ r.Right() - 8.0f - markWidth,
                                 r.y + 4.0f + (8.0f - mark.w) * markPixel * 0.5f,
                                 markWidth, mark.w * markPixel };
            m_renderer.UIAtlas(m_renderer.AtlasUV(mark.x, mark.y, mark.z, mark.w), markRect,
                               m_theme.textStrong);

            const std::string muster = unit->Wounded() > 0
                ? std::to_string(unit->Strength()) + " чол. (+" + std::to_string(unit->Wounded()) + ")"
                : std::to_string(unit->Strength()) + " чол.";
            m_ui.LabelRight({ r.x, r.y + 4.0f, r.w - 18.0f - 13.0f * markPixel, 18.0f }, muster, m_theme.textStrong);

            // Its own pace in the same units the column's is given in, so the two can be
            // compared without translating anything in one's head.
            const f32 speed = unit->Stats().speed;
            const bool laggard = speed <= slowest + 0.001f && cohort.units.size() > 1;
            m_renderer.UIText("Хід " + FormatNumber(speed * baseSpeed * supplyFactor),
                              { r.x + 10.0f, r.y + 21.0f },
                              laggard ? m_theme.negative : m_theme.textDim, 0.85f);

            m_ui.ProgressBar({ r.x + 8.0f, r.Bottom() - 9.0f, r.w - 16.0f, 5.0f },
                           unit->StrengthFraction(), ValueColor(unit->StrengthFraction(), m_theme));
            // Drill sits beside the strength bar: a full company of half-trained men is
            // not the same thing as a full company, and the panel should not pretend it is.
            m_ui.ProgressBar({ r.x + 8.0f, r.Bottom() - 3.0f, r.w - 16.0f, 3.0f },
                             unit->training, m_theme.textStrong);

            m_ui.TooltipIfHovered(r, role.name + " · власний хід " +
                FormatNumber(speed * baseSpeed * supplyFactor) + " за день" +
                "\nВишкіл: " + Percent(unit->training) + " (повний за " +
                FormatNumber(unit->Stats().trainDays) + " дн. у гарнізоні)" +
                (laggard ? "\n\nСаме цей підрозділ і стримує всю колону." : ""));
        }

        // --- orders --------------------------------------------------------------------------------
        const State* humanState = m_world.HumanState();
        if (clan && humanState && clan->state == humanState->id)
        {
            y += 10.0f;
            // Toggle flips the flag itself, so the value below is already what the player
            // has just asked for. The order is what makes it true on the host as well.
            const Rect raid = row(24.0f);
            bool raiding = cohort.mayRaid;
            if (m_ui.Toggle(raid, "Грабувати поселення", raiding))
            {
                // Alone the flag flips at once; in a party it flips at the order's tick, on
                // every machine together - setting it here too would put this one ahead.
                if (!OrdersDeferred()) cohort.mayRaid = raiding;
                Order(GameCommands::SetRaiding(cohort.id, raiding));
            }
            m_ui.TooltipIfHovered(raid, "Дозволяє загону грабувати ворожі поселення (Shift + ПКМ). "
                                    "Грабунок і захоплення можливі лише під час війни.");

            const Rect suppress = row(24.0f);
            bool suppressing = cohort.suppressRevolts;
            if (m_ui.Toggle(suppress, "Усмиряти повстання", suppressing))
            {
                // Turning it off stops the march it is already on, so the order is not a
                // thing the player has to chase after.
                if (!OrdersDeferred()) cohort.suppressRevolts = suppressing;
                Order(GameCommands::SetSuppressing(cohort.id, suppressing));
            }
            m_ui.TooltipIfHovered(suppress,
                "Загін сам іде на найближче повстання в межах держави й тримає його,\n"
                "доки поселення не вернеться під руку. Вимкнено за звичаєм: княже\n"
                "польове військо не має розбігатися по бунтівних селах без наказу.");

            const Rect stop = row(26.0f);
            if (m_ui.Button(stop, "Зупинити"))
            {
                Order(GameCommands::Stop(cohort.id));
            }

            // Retreat is a battlefield decision and belongs nowhere else: outside a fight
            // there is nothing to break contact with, so the button is simply not there.
            // While there is, it is the loudest thing on the panel - orange, because the
            // moment to use it is the moment the player is least inclined to look for it.
            if (battle)
            {
                const Rect leave = row(30.0f);
                if (m_ui.HighlightButton(leave, "ВІДСТУПИТИ", m_theme.warning))
                {
                    Order(GameCommands::Withdraw(cohort.id));
                    m_status = cohort.DisplayName() + " відходить";
                    m_statusTimer = 2.5f;
                }
                m_ui.TooltipIfHovered(leave,
                    "Вийти з бою. Ворог устигне вдарити навздогін, а лад похитнеться -\n"
                    "але військо збережеться.");
            }

            // The reverse of dividing: several banners under one. Only worth offering when
            // the player actually has a band selected, so it appears with the band and goes
            // away with it.
            if (m_selectedCohorts.size() >= 2)
            {
                std::vector<Cohort*> selected = CommandableSelection();
                // Only the hosts that have actually come up to this one can join it.
                const f32 reach = ConfigManager::Get().Float("movement/mergeDistance", 40.0f);
                std::vector<Cohort*> band;
                for (Cohort* host : selected)
                {
                    if (host->id == cohort.id || Distance(host->position, cohort.position) <= reach)
                        band.push_back(host);
                }
                const bool anyoneClose = std::any_of(band.begin(), band.end(),
                    [&](const Cohort* host) { return host->id != cohort.id; });
                if (!anyoneClose) band.clear();
                const size_t apart = selected.size() - (anyoneClose ? band.size() : 1);
                u32 units = 0;
                for (const Cohort* host : band) units += static_cast<u32>(host->units.size());
                const u32 limit = UnitDatabase::Get().MaxUnitsPerCohort();

                const Rect merge = row(26.0f);
                if (m_ui.Button(merge, "Звести докупи (" + std::to_string(std::max<size_t>(band.size(), 1)) +
                                       "/" + std::to_string(selected.size()) + ")",
                                band.size() >= 2))
                {
                    std::vector<EntityId> ids;
                    ids.reserve(band.size());
                    // The host whose panel is open leads; the rest fall in behind it.
                    ids.push_back(cohort.id);
                    for (const Cohort* host : band)
                    {
                        if (host->id != cohort.id) ids.push_back(host->id);
                    }

                    if (Order(GameCommands::Merge(ids)) || OrdersDeferred())
                    {
                        SelectCohort(cohort.id, false);
                        m_status = "Війська зведено докупи";
                        m_statusTimer = 2.5f;
                        m_ui.EndScroll(y);
                        return;
                    }
                    m_status = "Звести не вдалося: більше нема куди";
                    m_statusTimer = 3.0f;
                }
                m_ui.TooltipIfHovered(merge, band.size() < 2
                    ? std::string("Загони мають зійтися впритул, щоб стати одним військом. "
                                  "Відведіть їх в одне місце.")
                    : units > limit
                    ? "Під одним стягом більше ніж " + std::to_string(limit) +
                      " підрозділів не ходить — зайві лишаться окремо."
                    : std::string("Загони, що стоять поруч, стануть одним. Лад буде за найгіршим із них.") +
                      (apart > 0 ? "\n" + std::to_string(apart) + " — надто далеко й лишаться окремо." : ""));
            }

            // Dividing a host costs it some of its order, which is the price of the freedom
            // to be in two places at once.
            const bool divisible = cohort.units.size() >= 2;
            const Rect picker = row(26.0f);
            if (m_ui.Button(picker, m_splitPickerOpen ? "Згорнути вибір" : "Вибрати підрозділи для відділення",
                            divisible))
            {
                m_splitPickerOpen = !m_splitPickerOpen;
                m_splitUnits.clear();
            }
            m_ui.TooltipIfHovered(picker, divisible
                ? "Позначте підрозділи, які підуть окремим загоном."
                : "Щоб ділити, треба щонайменше два підрозділи.");

            if (m_splitPickerOpen && divisible)
            {
                y += 4.0f;
                for (EntityId unitId : cohort.units)
                {
                    Unit* unit = m_world.FindUnit(unitId);
                    if (!unit) continue;

                    const RoleInfo& role = UnitDatabase::Get().Role(unit->role);
                    const auto found = std::find(m_splitUnits.begin(), m_splitUnits.end(), unitId);
                    bool taken = found != m_splitUnits.end();

                    const Rect r = row(24.0f);
                    // Toggle flips the flag itself, so by the time it reports a click the
                    // value already says what the player wants - and the iterator is still
                    // good, because nothing has touched the list yet.
                    if (m_ui.Toggle(r, role.name + "  (" + std::to_string(unit->Strength()) + " чол.)", taken))
                    {
                        if (taken) m_splitUnits.push_back(unitId);
                        else if (found != m_splitUnits.end()) m_splitUnits.erase(found);
                    }
                }

                // Everything cannot leave: a host with nothing left in it is not a split.
                const bool viable = !m_splitUnits.empty() && m_splitUnits.size() < cohort.units.size();
                const Rect confirm = row(26.0f);
                if (m_ui.Button(confirm, "Відділити позначені", viable))
                {
                    if (Order(GameCommands::Split(cohort.id, m_splitUnits)) || OrdersDeferred())
                    {
                        m_splitPickerOpen = false;
                        m_splitUnits.clear();
                        m_status = "Загін відділено";
                        m_statusTimer = 2.5f;
                        m_ui.EndScroll(y);
                        return;
                    }
                }
                m_ui.TooltipIfHovered(confirm, viable
                    ? "Позначені підрозділи стануть окремим загоном. Лад обох частин потерпає."
                    : "Щось має лишитися й у старому загоні.");

                const Rect half = row(26.0f);
                if (m_ui.Button(half, "Або просто надвоє"))
                {
                    if (Order(GameCommands::SplitInHalf(cohort.id)) || OrdersDeferred())
                    {
                        m_splitPickerOpen = false;
                        m_splitUnits.clear();
                        m_status = "Загін розділено";
                        m_statusTimer = 2.5f;
                        m_ui.EndScroll(y);
                        return;
                    }
                }
            }

            const Rect garrison = row(26.0f);
            const Settlement* nearest = m_world.NearestSettlement(cohort.position, 1e9f, clan->id);
            if (m_ui.Button(garrison, nearest ? "У гарнізон: " + nearest->name : "У гарнізон", nearest != nullptr))
            {
                if (nearest)
                {
                    Order(GameCommands::Task(cohort.id, TaskType::Garrison,
                                             nearest->position, nearest->id));
                }
            }

            // Sending a host home cannot be taken back, so it asks once before it does it.
            y += 6.0f;
            const Rect disband = row(26.0f);
            const bool arming = m_disbandTarget == cohort.id;
            if (m_ui.Button(disband, arming ? "Справді розпустити?" : "Розпустити військо"))
            {
                if (arming)
                {
                    const std::string name = cohort.DisplayName();
                    m_disbandTarget = kInvalidId;
                    if (Order(GameCommands::Disband(cohort.id)) || OrdersDeferred())
                    {
                        m_selectionKind = SelectionKind::None;
                        m_selected = kInvalidId;
                        m_selectedUnit = kInvalidId;
                        m_selectedCohorts.clear();
                        m_status = name + " розпущено";
                        m_statusTimer = 3.0f;
                        m_ui.EndScroll(y);
                        return;
                    }
                }
                else
                {
                    m_disbandTarget = cohort.id;
                }
            }
            m_ui.TooltipIfHovered(disband,
                "Підрозділи буде розформовано, а люди повернуться до найближчого вашого поселення."
                "\n\nУтримання війська припиниться. Скасувати це не можна.");
        }

        m_ui.EndScroll(y);
    }

    void GameScene::DrawUnitDetails(const Rect& area, Unit& unit)
    {
        if (m_selectedCharacter != kInvalidId)
        {
            if (const Character* character = m_world.FindCharacter(m_selectedCharacter))
            {
                DrawCharacterDetails(area, *character);
                return;
            }
            m_selectedCharacter = kInvalidId;
        }

        const Rect view = area.Inset(m_theme.padding);
        const Rect back{ view.x, view.y, 90.0f, 22.0f };
        if (m_ui.Button(back, "< Назад")) m_selectedUnit = kInvalidId;

        const UnitData& stats = unit.Stats();
        const RoleInfo& role = UnitDatabase::Get().Role(unit.role);

        f32 y = view.y + 30.0f;
        auto row = [&]() { const Rect r{ view.x, y, view.w, m_theme.rowHeight }; y += m_theme.rowHeight + 2.0f; return r; };

        m_renderer.UIText(stats.name, { view.x, y }, m_theme.textStrong, 1.2f);
        y += 24.0f;
        m_renderer.UIText(role.name, { view.x, y }, m_theme.textDim);
        y += 22.0f;

        m_ui.KeyValue(row(), "Чисельність", std::to_string(unit.Strength()) + " / " +
                    std::to_string(unit.establishment), m_theme.text);
        m_ui.KeyValue(row(), "Атака", FormatNumber(stats.attack), m_theme.text);
        m_ui.KeyValue(row(), "Захист", FormatNumber(stats.defense), m_theme.text);
        m_ui.KeyValue(row(), "Здоров'я", FormatNumber(stats.health), m_theme.text);
        m_ui.KeyValue(row(), "Швидкість", FormatNumber(stats.speed), m_theme.text);
        m_ui.KeyValue(row(), "Дальність", FormatNumber(stats.range), m_theme.text);
        m_ui.KeyValue(row(), "Утримання", FormatNumber(stats.upkeep), m_theme.text);

        {
            const Rect r = row();
            m_ui.Label(r, m_theme.Label("morale"), m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, unit.morale,
                           ValueColor(unit.morale, m_theme), Percent(unit.morale));
        }
        {
            const Rect r = row();
            m_ui.Label(r, m_theme.Label("training"), m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, unit.training,
                           m_theme.accent, Percent(unit.training));
        }
        {
            const Rect r = row();
            m_ui.Label(r, "Втома", m_theme.textDim);
            m_ui.ProgressBar({ r.x + 110.0f, r.y + 5.0f, r.w - 110.0f, 12.0f }, unit.fatigue,
                           m_theme.warning, Percent(unit.fatigue));
        }

        y += 8.0f;
        m_ui.Label(row(), m_theme.Label("characters") + " (" + std::to_string(unit.Strength()) + ")",
                 m_theme.accent);

        // The whole roster, by name: this is the point of the design.
        const Rect listArea{ view.x, y, view.w, view.Bottom() - y };
        const f32 rowHeight = 20.0f;
        const Rect content = m_ui.BeginScroll(listArea, unit.characters.size() * rowHeight, m_sidePanelScroll);

        for (size_t i = 0; i < unit.characters.size(); ++i)
        {
            const Character* person = m_world.FindCharacter(unit.characters[i]);
            if (!person) continue;

            const Rect r{ content.x, content.y + i * rowHeight, content.w, rowHeight - 1.0f };
            if (m_ui.ListItem(r, person->FullName(), false))
            {
                m_selectedCharacter = person->id;
            }
            m_ui.LabelRight(r, std::to_string(person->age), m_theme.textDim);
        }
        m_ui.EndScroll();
    }

    void GameScene::DrawCharacterDetails(const Rect& area, const Character& character)
    {
        const Rect view = area.Inset(m_theme.padding);
        const Rect back{ view.x, view.y, 90.0f, 22.0f };
        if (m_ui.Button(back, "< Назад")) m_selectedCharacter = kInvalidId;

        const RaceInfo& race = RaceDatabase::Get().Race(character.raceId);

        f32 y = view.y + 32.0f;
        m_renderer.UISprite(race.sprite, { view.x, y, 40.0f, 40.0f }, race.color);
        m_renderer.UIText(character.FullName(), { view.x + 48.0f, y + 4.0f }, m_theme.textStrong, 1.15f);
        m_renderer.UIText(race.name, { view.x + 48.0f, y + 24.0f }, m_theme.textDim);
        y += 52.0f;

        auto row = [&]() { const Rect r{ view.x, y, view.w, m_theme.rowHeight }; y += m_theme.rowHeight + 2.0f; return r; };

        char buffer[48];
        m_ui.KeyValue(row(), m_theme.Label("age"), std::to_string(character.age) + " років", m_theme.text);
        std::snprintf(buffer, sizeof(buffer), "%.0f см", character.height);
        m_ui.KeyValue(row(), m_theme.Label("height"), buffer, m_theme.text);
        std::snprintf(buffer, sizeof(buffer), "%.0f кг", character.weight);
        m_ui.KeyValue(row(), m_theme.Label("weight"), buffer, m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("origin"), character.origin.empty() ? "—" : character.origin, m_theme.text);
        m_ui.KeyValue(row(), "Стать", character.gender == Gender::Male ? "чоловіча" : "жіноча", m_theme.text);

        if (!character.traits.empty())
        {
            y += 6.0f;
            m_ui.Label(row(), "РИСИ", m_theme.accent);
            for (const std::string& traitId : character.traits)
            {
                const Trait* trait = UnitDatabase::Get().FindTrait(traitId);
                const Rect r = row();
                m_ui.Label(r, "· " + (trait ? trait->name : traitId), m_theme.text);
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
                    m_ui.TooltipIfHovered(r, effects.empty() ? trait->name : effects);
                }
            }
        }

        // --- dynasty ------------------------------------------------------------------------------
        if (character.noble)
        {
            y += 8.0f;
            m_ui.Label(row(), "РІД", m_theme.accent);
            if (const Clan* clan = m_world.FindClan(character.clan))
            {
                m_ui.KeyValue(row(), "Дім", clan->name, clan->color);
                m_ui.KeyValue(row(), "Голова", clan->head == character.id ? "так" : "немає", m_theme.text);
            }

            auto namedRelative = [&](const char* label, EntityId id)
            {
                if (id == kInvalidId) return;
                const Character* relative = m_world.FindCharacter(id);
                if (!relative) return;
                m_ui.KeyValue(row(), label, relative->FullName() +
                            (relative->alive ? "" : " (†)"), m_theme.text);
            };
            namedRelative("Батько", character.father);
            namedRelative("Мати", character.mother);
            namedRelative("Дружина/чоловік", character.spouse);

            if (!character.children.empty())
            {
                m_ui.Label(row(), "Діти", m_theme.textDim);
                for (EntityId childId : character.children)
                {
                    if (const Character* child = m_world.FindCharacter(childId))
                    {
                        m_ui.Label(row(), "  · " + child->FullName() + ", " +
                                 std::to_string(child->age), m_theme.text);
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
        const State* state = m_world.HumanState();
        if (!state)
        {
            m_ui.LabelCentered({ area.x, area.y + 30.0f, area.w, 24.0f }, "Держава впала", m_theme.negative);
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
            const Clan* clan = m_world.FindClan(clanId);
            if (!clan) continue;
            treasury += clan->resources;
            net += EconomySystem::Get().Preview(m_world, clanId).net;
            tiles += CoverageSystem::Get().TilesOwnedBy(clanId);
            settlementCount += clan->settlements.size();
            cohortCount += clan->cohorts.size();

            for (EntityId settlementId : clan->settlements)
            {
                if (const Settlement* settlement = m_world.FindSettlement(settlementId))
                    population += settlement->population;
            }
            for (EntityId cohortId : clan->cohorts) soldiers += m_world.CohortStrength(cohortId);
        }

        const Rect view = area.Inset(m_theme.padding);
        f32 contentHeight = 300.0f + state->clans.size() * 26.0f + settlementCount * 24.0f;
        const Rect content = m_ui.BeginScroll(view, contentHeight, m_realmScroll);

        f32 y = content.y;
        auto row = [&]() { const Rect r{ content.x, y, content.w, m_theme.rowHeight }; y += m_theme.rowHeight + 2.0f; return r; };

        m_renderer.UIText(state->name, { content.x, y }, m_theme.textStrong, 1.25f);
        y += 28.0f;

        m_ui.KeyValue(row(), m_theme.Label("population"), std::to_string(population), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("settlements"), std::to_string(settlementCount), m_theme.text);
        m_ui.KeyValue(row(), m_theme.Label("cohorts"), std::to_string(cohortCount), m_theme.text);
        m_ui.KeyValue(row(), "Воїнів", std::to_string(soldiers), m_theme.text);
        m_ui.KeyValue(row(), "Земель (клітин)", std::to_string(tiles), m_theme.text);

        y += 6.0f;
        m_ui.Label(row(), "СКАРБНИЦЯ", m_theme.accent);
        m_ui.KeyValue(row(), m_theme.Label("money"), FormatNumber(treasury.money) + "  (" + Signed(net.money) + ")",
                    net.money >= 0.0f ? m_theme.positive : m_theme.negative);
        m_ui.KeyValue(row(), m_theme.Label("food"), FormatNumber(treasury.food) + "  (" + Signed(net.food) + ")",
                    net.food >= 0.0f ? m_theme.positive : m_theme.negative);
        m_ui.KeyValue(row(), m_theme.Label("wood"), FormatNumber(treasury.wood) + "  (" + Signed(net.wood) + ")",
                    net.wood >= 0.0f ? m_theme.positive : m_theme.negative);
        m_ui.KeyValue(row(), m_theme.Label("stone"), FormatNumber(treasury.stone) + "  (" + Signed(net.stone) + ")",
                    net.stone >= 0.0f ? m_theme.positive : m_theme.negative);

        y += 6.0f;
        m_ui.Label(row(), m_theme.Label("clans"), m_theme.accent);
        for (EntityId clanId : state->clans)
        {
            const Clan* clan = m_world.FindClan(clanId);
            if (!clan) continue;
            const Rect r = row();
            m_ui.ListItem(r, clan->name, clanId == state->leader, clan->color);

            const Character* head = m_world.FindCharacter(clan->head);
            m_ui.LabelRight(r, head ? head->FullName() : "—", m_theme.textDim);
        }

        // --- the hosts ----------------------------------------------------------------------
        // Every army the realm has, with what it is doing and how many men are in it: the
        // one place a player can weigh his whole strength without hunting over the map.
        y += 6.0f;
        m_ui.Label(row(), "ВІЙСЬКА", m_theme.accent);
        for (EntityId clanId : state->clans)
        {
            const Clan* clan = m_world.FindClan(clanId);
            if (!clan) continue;
            for (EntityId cohortId : clan->cohorts)
            {
                const Cohort* cohort = m_world.FindCohort(cohortId);
                if (!cohort) continue;

                const Rect r{ content.x, y, content.w, 30.0f };
                y += 32.0f;
                // The row is drawn empty and written on by hand: two lines of different
                // weight do not fit the single centred label a list item draws for itself.
                if (m_ui.ListItem(r, "", IsSelected(cohortId), clan->color))
                {
                    SelectCohort(cohortId, false);
                    m_panelMode = PanelMode::Selection;
                    m_renderer.GetCamera().SetFocus(cohort->position);
                }
                m_renderer.UIText(cohort->DisplayName(), { r.x + 10.0f, r.y + 2.0f }, m_theme.textStrong);
                m_renderer.UIText(Task::TypeName(cohort->currentTask.type),
                                  { r.x + 10.0f, r.y + 17.0f }, m_theme.textDim, 0.85f);
                m_ui.LabelRight({ r.x, r.y + 4.0f, r.w - 6.0f, 16.0f },
                                std::to_string(m_world.CohortStrength(cohortId)) + " чол.",
                                m_theme.textStrong);
            }
        }

        y += 6.0f;
        m_ui.Label(row(), m_theme.Label("settlements"), m_theme.accent);
        for (EntityId clanId : state->clans)
        {
            const Clan* clan = m_world.FindClan(clanId);
            if (!clan) continue;
            for (EntityId settlementId : clan->settlements)
            {
                Settlement* settlement = m_world.FindSettlement(settlementId);
                if (!settlement) continue;

                const Rect r{ content.x, y, content.w, 22.0f };
                y += 24.0f;
                if (m_ui.ListItem(r, settlement->name, settlementId == m_selected, clan->color))
                {
                    m_selectionKind = SelectionKind::Settlement;
                    m_selected = settlementId;
                    m_panelMode = PanelMode::Selection;
                    m_renderer.GetCamera().SetFocus(settlement->position);
                }
                m_ui.LabelRight(r, std::to_string(settlement->population), m_theme.textDim);
            }
        }

        m_ui.EndScroll(y);
    }

    void GameScene::DrawDiplomacyPanel(const Rect& area)
    {
        DiplomacySystem& diplomacy = DiplomacySystem::Get();

        const State* self = m_world.HumanState();
        if (!self) return;

        const Rect view = area.Inset(m_theme.padding);
        f32 y = view.y;
        auto row = [&](f32 height)
        {
            const Rect r{ view.x, y, view.w, height };
            y += height + 3.0f;
            return r;
        };

        m_ui.Label(row(m_theme.rowHeight), "ДЕРЖАВИ", m_theme.accent);

        std::vector<EntityId> others;
        for (const auto& [id, state] : m_world.States())
        {
            if (id != self->id && !state.eliminated && !state.outlaw) others.push_back(id);
        }
        std::sort(others.begin(), others.end());

        for (EntityId id : others)
        {
            const State* other = m_world.FindState(id);
            if (!other) continue;

            const Rect r = row(26.0f);
            // A realm never seen is a blank on the map: no name, no banner, no embassy.
            if (!diplomacy.Known(m_world, self->id, id))
            {
                m_ui.ListItem(r, "Невідома держава", false, m_theme.textDim);
                m_ui.LabelRight(r, "Невідомо", m_theme.textDim);
                m_ui.TooltipIfHovered(r, "Ваші люди ще не зустрічали цієї держави: ані її війська, ані її поселень.\n"
                                         "Поки не зустрінуть — з нею не можна вести жодних справ.");
                if (m_diplomacyTarget == id) m_diplomacyTarget = kInvalidId;
                continue;
            }
            if (m_ui.ListItem(r, other->name, id == m_diplomacyTarget, other->color))
            {
                m_diplomacyTarget = id;
            }

            const DiplomaticStance stance = self->StanceWith(id);
            const Color stanceColor = stance == DiplomaticStance::War ? m_theme.negative
                                    : stance == DiplomaticStance::Alliance ? m_theme.positive
                                    : m_theme.textDim;
            m_ui.LabelRight(r, State::StanceName(stance), stanceColor);
        }

        if (m_diplomacyTarget == kInvalidId) return;
        const State* target = m_world.FindState(m_diplomacyTarget);
        if (!target) { m_diplomacyTarget = kInvalidId; return; }

        y += 8.0f;
        m_ui.Label(row(m_theme.rowHeight), "СТАВЛЕННЯ", m_theme.accent);

        const f32 opinion = diplomacy.Opinion(m_world, self->id, m_diplomacyTarget);
        {
            const Rect r = row(m_theme.rowHeight);
            m_ui.Label(r, target->name, m_theme.text);
            m_ui.LabelRight(r, Signed(opinion),
                          opinion >= 0.0f ? m_theme.positive : m_theme.negative);
        }
        for (const auto& [reason, value] : diplomacy.OpinionBreakdown(m_world, self->id, m_diplomacyTarget))
        {
            const Rect r = row(18.0f);
            m_ui.Label(r, "  " + reason, m_theme.textDim);
            m_ui.LabelRight(r, Signed(value), value >= 0.0f ? m_theme.positive : m_theme.negative);
        }

        y += 8.0f;
        m_ui.Label(row(m_theme.rowHeight), m_theme.Label("actions"), m_theme.accent);
        for (const DiplomaticAction& action : diplomacy.AvailableActions(m_world, self->id, m_diplomacyTarget))
        {
            const Rect r = row(26.0f);
            if (m_ui.Button(r, action.label, action.available))
            {
                if (Order(GameCommands::Diplomacy(m_diplomacyTarget, static_cast<i32>(action.kind))) ||
                    OrdersDeferred())
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
            m_ui.TooltipIfHovered(r, action.tooltip);
        }
    }

    void GameScene::DrawChroniclePanel(const Rect& area)
    {
        const std::vector<Chronicle>& entries = m_world.ChronicleEntries();
        const Rect view = area.Inset(m_theme.padding);
        const f32 rowHeight = m_renderer.TextHeight() + 6.0f;

        const Rect content = m_ui.BeginScroll(view, entries.size() * rowHeight, m_chronicleScroll);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const size_t index = entries.size() - 1 - i;   // newest first
            const Rect r{ content.x, content.y + i * rowHeight, content.w, rowHeight };
            m_renderer.UIText(entries[index].text, { r.x, r.y + 2.0f }, entries[index].color);
        }
        m_ui.EndScroll();
    }

    // =====================================================================================
    // Hover tooltip over the map
    // =====================================================================================

    void GameScene::DrawTooltipForHover()
    {
        const RaceDatabase& races = RaceDatabase::Get();

        if (m_hoveredSettlement != kInvalidId)
        {
            const Settlement* settlement = m_world.FindSettlement(m_hoveredSettlement);
            if (!settlement) return;
            const Clan* owner = m_world.FindClan(settlement->owner);

            std::string text = settlement->name + " — " + settlement->TierName() + "\n";
            text += "Власник: " + (owner ? owner->name : std::string("незалежне")) + "\n";
            text += "Народ: " + races.Race(settlement->raceId).name +
                    ", віра: " + races.Faith(settlement->faithId).name + "\n";
            text += "Населення: " + std::to_string(settlement->population) +
                    ", вірність: " + Percent(settlement->loyalty);
            m_ui.Tooltip(text);
            return;
        }

        if (m_hoveredCohort != kInvalidId)
        {
            const Cohort* cohort = m_world.FindCohort(m_hoveredCohort);
            if (!cohort) return;
            const Clan* clan = m_world.FindClan(cohort->clan);

            std::string text = cohort->DisplayName() + "\n";
            text += (clan ? clan->name : std::string("—")) + "\n";
            text += "Людей: " + std::to_string(m_world.CohortStrength(cohort->id)) +
                    ", досвід: " + Percent(cohort->experience) + "\n";
            text += std::string("Наказ: ") + Task::TypeName(cohort->currentTask.type);
            m_ui.Tooltip(text);
        }
    }
}
