// GameSceneHud.cpp - every panel of the in-game interface.
//
// Split out of GameScene.cpp so the scene file stays about the world and this one stays
// about presenting it. All of it is immediate-mode: what you see is what this frame drew.
#include "GameScene.h"
#include "SceneManager.h"

#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Game/Factories/EvaluatorFactory.h"
#include "../Game/Systems/BattleSystem.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/Systems/DiplomacySystem.h"
#include "../Game/Systems/DynastySystem.h"
#include "../Game/Systems/EconomySystem.h"
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
            if (m_ui.InvisibleButton(button, "speed" + label)) simulation.SetSpeedIndex(static_cast<i32>(i));
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

        DrawConstructionPanel(body);
    }

    // =====================================================================================
    // Construction: what a realm raises on the map itself
    // =====================================================================================

    void GameScene::DrawConstructionPanel(const Rect& area)
    {
        const Rect view = area.Inset(m_theme.padding);
        f32 y = view.y;
        auto row = [&](f32 height)
        {
            const Rect r{ view.x, y, view.w, height };
            y += height + 4.0f;
            return r;
        };

        m_ui.LabelCentered(row(22.0f), "Нічого не вибрано", m_theme.textDim);
        m_ui.LabelCentered(row(20.0f), "ЛКМ — вибрати, ПКМ — наказ", m_theme.textDim);

        Clan* clan = m_world.HumanClan();
        if (!clan) return;

        y += 10.0f;
        m_ui.Label(row(20.0f), "БУДІВНИЦТВО", m_theme.accent);
        y += m_ui.Paragraph({ view.x, y, view.w, 0.0f },
                            "Тут зводять те, що стоїть на самій карті. Усе інше — казарми, "
                            "поля, стіни — це покращення поселень, і робиться в їхніх панелях.",
                            m_theme.textDim) + 10.0f;

        if (m_placingSettlement)
        {
            const SettlementKindInfo& info = SettlementDatabase::Get().Kind(m_placingKind);
            m_ui.Label(row(22.0f), "Оберіть місце: " + info.name, m_theme.warning);
            if (m_ui.Button(row(26.0f), "Скасувати")) m_placingSettlement = false;
            return;
        }

        const SettlementDatabase& db = SettlementDatabase::Get();
        for (int i = 0; i < 3; ++i)
        {
            const SettlementKind kind = static_cast<SettlementKind>(i);
            const SettlementKindInfo& info = db.Kind(kind);

            const Rect r = row(28.0f);
            const bool affordable = clan->resources.CanAfford(info.buildCost);
            if (m_ui.Button(r, "Заснувати: " + info.name, true, affordable))
            {
                m_placingSettlement = true;
                m_placingKind = kind;
                m_status = "Оберіть місце на карті";
                m_statusTimer = 4.0f;
            }

            m_ui.TooltipIfHovered(r,
                "Ціна: " + FormatNumber(info.buildCost.money) + " срібла, " +
                FormatNumber(info.buildCost.wood) + " дерева, " +
                FormatNumber(info.buildCost.stone) + " каменю\n"
                "Переселенців: " + std::to_string(info.settlers) + " з довколишніх поселень\n"
                "Народ і віра — ваші власні." +
                std::string(affordable ? "" : "\n\nБракує коштів"));
        }

        y += 8.0f;
        m_ui.Label(row(20.0f), "ШЛЯХИ", m_theme.accent);
        m_ui.Paragraph({ view.x, y, view.w, 0.0f },
                       "Дорогу прокладають від поселення: виберіть його й відкрийте "
                       "«Прокласти шлях».", m_theme.textDim);
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
            Clan* clan = m_world.HumanClan();
            const EntityId created = clan
                ? SettlementSystem::Get().Found(m_world, clan->id, m_placingKind, m_pendingSite, m_pendingName)
                : kInvalidId;

            if (created != kInvalidId)
            {
                m_selectionKind = SelectionKind::Settlement;
                m_selected = created;
                m_settlementTab = SettlementTab::Overview;
                m_status = "Поселення засновано";
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
        if (playerOwns && m_settlementTab == SettlementTab::Overview)
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
            if (info) m_ui.TooltipIfHovered(r, info->description);
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
                    settlements.CancelConstruction(m_world, settlement.id, order.buildingId);
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
            const Rect r{ content.x, y, content.w, 26.0f };
            y += 28.0f;

            if (m_ui.Button(r, option.building->name, option.allowed, option.affordable))
            {
                settlements.StartConstruction(m_world, settlement.id, option.building->id);
            }

            const ResourceData& cost = option.building->cost;
            std::string tooltip = option.building->description + "\n\nЦіна: " +
                FormatNumber(cost.money) + " срібла, " + FormatNumber(cost.wood) + " дерева, " +
                FormatNumber(cost.stone) + " каменю\nТермін: " +
                std::to_string(option.building->buildDays) + " дн.";
            if (!option.allowed && !option.blockedReason.empty()) tooltip += "\n\n" + option.blockedReason;
            else if (!option.affordable) tooltip += "\n\nБракує коштів";
            m_ui.TooltipIfHovered(r, tooltip);
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

        const std::vector<RecruitOption> options = settlements.RecruitOptions(m_world, settlement.id);
        for (const RecruitOption& option : options)
        {
            const Rect r{ content.x, y, content.w, 26.0f };
            y += 28.0f;

            const std::string label = option.name + "  (" + std::to_string(option.headCount) + ")";
            if (m_ui.Button(r, label, option.blockedReason.empty() || option.affordable, option.affordable))
            {
                const EntityId result = settlements.Recruit(m_world, settlement.id, destination, option.role);
                if (result != kInvalidId)
                {
                    m_status = "Загін набрано";
                    m_statusTimer = 2.5f;
                }
            }
            std::string tooltip = "Ціна: " + FormatNumber(option.cost) + " срібла";
            if (!option.affordable && !option.blockedReason.empty()) tooltip += "\n" + option.blockedReason;
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
            const Rect r{ content.x, y, content.w, 26.0f };
            y += 28.0f;

            const bool affordable = plan.valid && owner->resources.CanAfford(plan.cost);
            const std::string label = other->name + "  (" +
                FormatNumber(plan.metres / 1000.0f) + " км, " + FormatNumber(plan.cost.money) + ")";

            if (m_ui.Button(r, label, plan.valid, affordable))
            {
                roads.Begin(m_world, owner->id, plan);
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
            const Rect r{ content.x, y, content.w, 26.0f };
            y += 28.0f;

            const f32 cost = settlements.ConversionCost(m_world, settlement.id, owner->faithId);
            if (m_ui.Button(r, "Навернути до віри " + faith.name + " (" + FormatNumber(cost) + ")",
                            true, owner->resources.money >= cost))
            {
                settlements.StartConversion(m_world, settlement.id, owner->faithId);
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
                settlements.GrantIndependence(m_world, settlement.id);
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
                settlements.Raze(m_world, settlement.id, owner->id);
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
            const f32 got = market.Trade(m_world, settlement.id, clan->id,
                                         m_tradeGive, m_tradeTake, amount);
            if (got > 0.0f)
            {
                m_status = "Обміняно на " + FormatNumber(got) + " " +
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

        const Rect button = row(30.0f);
        if (m_ui.Button(button, "Освоїти каменярню", offer.allowed, offer.affordable))
        {
            if (settlements.DevelopMine(m_world, clanId, mine.id))
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
        const Rect view = area.Inset(m_theme.padding);

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
            const std::string muster = unit->Wounded() > 0
                ? std::to_string(unit->Strength()) + " чол. (+" + std::to_string(unit->Wounded()) + ")"
                : std::to_string(unit->Strength()) + " чол.";
            m_ui.LabelRight({ r.x, r.y + 4.0f, r.w - 6.0f, 18.0f }, muster, m_theme.textStrong);

            // Its own pace in the same units the column's is given in, so the two can be
            // compared without translating anything in one's head.
            const f32 speed = unit->Stats().speed;
            const bool laggard = speed <= slowest + 0.001f && cohort.units.size() > 1;
            m_renderer.UIText("Хід " + FormatNumber(speed * baseSpeed * supplyFactor),
                              { r.x + 10.0f, r.y + 21.0f },
                              laggard ? m_theme.negative : m_theme.textDim, 0.85f);

            m_ui.ProgressBar({ r.x + 8.0f, r.Bottom() - 9.0f, r.w - 16.0f, 5.0f },
                           unit->StrengthFraction(), ValueColor(unit->StrengthFraction(), m_theme));
            m_ui.TooltipIfHovered(r, role.name + " · власний хід " +
                FormatNumber(speed * baseSpeed * supplyFactor) + " за день" +
                (laggard ? "\n\nСаме цей підрозділ і стримує всю колону." : ""));
        }

        // --- orders --------------------------------------------------------------------------------
        const State* humanState = m_world.HumanState();
        if (clan && humanState && clan->state == humanState->id)
        {
            y += 10.0f;
            const Rect raid = row(24.0f);
            if (m_ui.Toggle(raid, "Грабувати поселення", cohort.mayRaid) &&
                !cohort.mayRaid && cohort.currentTask.type == TaskType::Raid)
            {
                cohort.currentTask.Clear();
            }
            m_ui.TooltipIfHovered(raid, "Дозволяє загону грабувати ворожі поселення (Shift + ПКМ). "
                                    "Грабунок і захоплення можливі лише під час війни.");

            const Rect suppress = row(24.0f);
            if (m_ui.Toggle(suppress, "Усмиряти повстання", cohort.suppressRevolts))
            {
                // Turning it off stops the march it is already on, so the order is not a
                // thing the player has to chase after.
                if (!cohort.suppressRevolts && cohort.currentTask.type == TaskType::Attack)
                {
                    cohort.currentTask.Clear();
                }
            }
            m_ui.TooltipIfHovered(suppress,
                "Загін сам іде на найближче повстання в межах держави й тримає його,\n"
                "доки поселення не вернеться під руку. Вимкнено за звичаєм: княже\n"
                "польове військо не має розбігатися по бунтівних селах без наказу.");

            const Rect stop = row(26.0f);
            if (m_ui.Button(stop, "Зупинити"))
            {
                cohort.currentTask.Clear();
            }

            // Leaving a fight is always allowed. It costs order and a parting blow, and it
            // is very often the right thing to do.
            const Rect leave = row(26.0f);
            const bool fighting = battle != nullptr;
            if (m_ui.Button(leave, "Відступити", fighting || cohort.currentTask.IsMoving()))
            {
                BattleSystem::Get().Withdraw(m_world, cohort.id);
                m_status = cohort.DisplayName() + " відходить";
                m_statusTimer = 2.5f;
            }
            m_ui.TooltipIfHovered(leave, fighting
                ? "Вийти з бою. Ворог устигне вдарити навздогін, а лад похитнеться -\nале військо збережеться."
                : "Відірватися й відійти.");

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
                    const EntityId fresh = MovementSystem::Get().Split(m_world, cohort.id, m_splitUnits);
                    if (fresh != kInvalidId)
                    {
                        m_splitPickerOpen = false;
                        m_splitUnits.clear();
                        SelectCohort(fresh, false);
                        m_status = "Загін відділено";
                        m_statusTimer = 2.5f;
                        return;
                    }
                }
                m_ui.TooltipIfHovered(confirm, viable
                    ? "Позначені підрозділи стануть окремим загоном. Лад обох частин потерпає."
                    : "Щось має лишитися й у старому загоні.");

                const Rect half = row(26.0f);
                if (m_ui.Button(half, "Або просто надвоє"))
                {
                    const EntityId fresh = MovementSystem::Get().SplitInHalf(m_world, cohort.id);
                    if (fresh != kInvalidId)
                    {
                        m_splitPickerOpen = false;
                        m_splitUnits.clear();
                        SelectCohort(fresh, false);
                        m_status = "Загін розділено";
                        m_statusTimer = 2.5f;
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
                    MovementSystem::Get().OrderTask(m_world, cohort.id, TaskType::Garrison,
                                                    nearest->position, nearest->id);
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
                    if (MovementSystem::Get().Disband(m_world, cohort.id))
                    {
                        m_selectionKind = SelectionKind::None;
                        m_selected = kInvalidId;
                        m_selectedUnit = kInvalidId;
                        m_selectedCohorts.clear();
                        m_status = name + " розпущено";
                        m_statusTimer = 3.0f;
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
            if (id != self->id && !state.eliminated) others.push_back(id);
        }
        std::sort(others.begin(), others.end());

        for (EntityId id : others)
        {
            const State* other = m_world.FindState(id);
            if (!other) continue;

            const Rect r = row(26.0f);
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
                if (diplomacy.Perform(m_world, self->id, m_diplomacyTarget, action.kind))
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
