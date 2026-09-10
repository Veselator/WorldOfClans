#include "PartySetupScene.h"
#include "SceneManager.h"
#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Core/Random.h"
#include "../Game/World/RaceDatabase.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace woc
{
    void PartySetupScene::OnEnter()
    {
        Renderer::Get().SetTerrainEnabled(false);
        Renderer::Get().SetClearColor(Theme::Get().background);

        m_maps = MapLoader::ListMaps();
        if (m_maps.empty())
        {
            m_error = "У теці Maps немає жодної карти";
        }
        else
        {
            m_selectedMap = 0;
            m_settings.mapFolder = m_maps[0].folder;
        }

        m_palette.clear();
        for (const Json& entry : ConfigManager::Get().Game()["clanColors"].AsArray())
        {
            m_palette.push_back(static_cast<u32>(std::strtoul(entry.AsString("c8452d").c_str(), nullptr, 16)));
        }
        if (m_palette.empty()) m_palette.push_back(0xC8452D);

        m_settings.stateCount = 5;
        m_settings.playerColor = m_palette.front();
        RandomiseColors();
    }

    void PartySetupScene::EnsureColorCount()
    {
        const size_t rivals = static_cast<size_t>(std::max(0, m_settings.stateCount - 1));
        while (m_settings.rivalColors.size() < rivals)
        {
            m_settings.rivalColors.push_back(m_palette[(m_settings.rivalColors.size() + 1) % m_palette.size()]);
        }
        m_settings.rivalColors.resize(rivals);
        m_colorTarget = std::clamp(m_colorTarget, -1, static_cast<i32>(rivals) - 1);
    }

    void PartySetupScene::RandomiseColors()
    {
        EnsureColorCount();

        // Deal distinct colours out of the palette so no two banners collide.
        std::vector<u32> pool = m_palette;
        Random random;
        random.Shuffle(pool);

        size_t cursor = 0;
        m_settings.playerColor = pool[cursor++ % pool.size()];
        for (u32& color : m_settings.rivalColors) color = pool[cursor++ % pool.size()];
    }

    void PartySetupScene::Update(f32)
    {
        // Everything happens through the widgets in Render.
    }

    void PartySetupScene::Render()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        const Vec2 viewport = renderer.ViewportSize();

        EnsureColorCount();

        const Rect header{ 0.0f, 0.0f, viewport.x, 44.0f };
        renderer.UIRect(header, theme.panelHeader);
        renderer.UIText("НАЛАШТУВАННЯ ПАРТІЇ", { 20.0f, 13.0f }, theme.accent, 1.3f);

        const f32 margin = 20.0f;
        const f32 top = header.Bottom() + margin;
        const f32 bottom = viewport.y - 70.0f;
        const f32 columnWidth = (viewport.x - margin * 4.0f) / 3.0f;

        // Columns slide in from the left, one after the other.
        const f32 slideA = ui.SlideIn("setup.a", true, 60.0f, 0.22f);
        const f32 slideB = ui.SlideIn("setup.b", true, 60.0f, 0.30f);
        const f32 slideC = ui.SlideIn("setup.c", true, 60.0f, 0.38f);

        RenderMapList({ margin - slideA, top, columnWidth, bottom - top });
        RenderOptions({ margin * 2.0f + columnWidth - slideB, top, columnWidth, bottom - top });
        RenderColors({ margin * 3.0f + columnWidth * 2.0f - slideC, top, columnWidth, bottom - top });

        // --- footer --------------------------------------------------------------------------
        const Rect back{ margin, viewport.y - 54.0f, 180.0f, 34.0f };
        if (ui.Button(back, "Назад"))
        {
            SceneManager::Get().Request(SceneId::MainMenu);
        }

        const Rect start{ viewport.x - margin - 240.0f, viewport.y - 54.0f, 240.0f, 34.0f };
        if (ui.Button(start, "Почати партію", !m_maps.empty()))
        {
            m_settings.seed = static_cast<u32>(std::strtoul(m_seedText.c_str(), nullptr, 10));

            const std::vector<RaceInfo>& races = RaceDatabase::Get().Races();
            if (!races.empty())
            {
                m_settings.playerRace = races[static_cast<size_t>(
                    std::clamp(m_selectedRace, 0, static_cast<i32>(races.size()) - 1))].id;
            }

            SceneManager& scenes = SceneManager::Get();
            scenes.SetPayload("map", m_settings.mapFolder);
            scenes.SetData("party", m_settings.ToJson());
            scenes.Request(SceneId::Game);
        }

        if (!m_error.empty())
        {
            renderer.UIText(m_error, { margin, viewport.y - 78.0f }, theme.negative);
        }
    }

    void PartySetupScene::RenderMapList(const Rect& area)
    {
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();

        ui.Panel(area, "Карта");
        const Rect body = Rect{ area.x, area.y + theme.headerHeight, area.w, area.h - theme.headerHeight }
                              .Inset(theme.padding);

        const f32 rowHeight = 46.0f;
        const Rect content = ui.BeginScroll(body, m_maps.size() * rowHeight, m_mapScroll);

        for (size_t i = 0; i < m_maps.size(); ++i)
        {
            const Rect row{ content.x, content.y + i * rowHeight, content.w, rowHeight - 4.0f };
            const bool selected = static_cast<i32>(i) == m_selectedMap;
            if (ui.ListItem(row, m_maps[i].name, selected))
            {
                m_selectedMap = static_cast<i32>(i);
                m_settings.mapFolder = m_maps[i].folder;
            }
            const std::string size = std::to_string(m_maps[i].width) + "x" + std::to_string(m_maps[i].height);
            ui.LabelRight({ row.x, row.y, row.w - theme.padding, row.h }, size, theme.textDim);

            if (!m_maps[i].description.empty()) ui.TooltipIfHovered(row, m_maps[i].description);
        }

        ui.EndScroll();
    }

    void PartySetupScene::RenderOptions(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        ui.Panel(area, "Умови");
        f32 y = area.y + theme.headerHeight + theme.padding;
        const f32 innerX = area.x + theme.padding;
        const f32 innerW = area.w - theme.padding * 2.0f;

        ui.Stepper({ innerX, y, innerW, 30.0f }, "Держав на карті", m_settings.stateCount, 2, 12);
        y += 40.0f;

        ui.Label({ innerX, y, innerW, 20.0f }, "Народ гравця", theme.textDim);
        y += 24.0f;

        const std::vector<RaceInfo>& races = RaceDatabase::Get().Races();
        for (size_t i = 0; i < races.size(); ++i)
        {
            const Rect row{ innerX, y, innerW, 40.0f };
            const bool selected = static_cast<i32>(i) == m_selectedRace;
            if (ui.ListItem(row, races[i].name, selected, races[i].color))
            {
                m_selectedRace = static_cast<i32>(i);
            }
            renderer.UISprite(races[i].sprite, { row.Right() - 34.0f, row.y + 4.0f, 32.0f, 32.0f },
                              races[i].color);
            ui.TooltipIfHovered(row, races[i].description);
            y += 44.0f;
        }

        y += 10.0f;
        ui.Label({ innerX, y, 120.0f, 26.0f }, "Зерно (seed)", theme.textDim);
        ui.TextField({ innerX + 130.0f, y, innerW - 130.0f, 26.0f }, "seed", m_seedText, 10);
        y += 30.0f;
        ui.Label({ innerX, y, innerW, 18.0f }, "0 — випадкове зерно світу", theme.textDim);
        y += 26.0f;

        ui.Toggle({ innerX, y, innerW, 26.0f }, "Випадкові народи в суперників", m_settings.randomiseRaces);
        y += 34.0f;

        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        // The race blurb and its modifiers: the actual reason to pick one.
        if (!races.empty())
        {
            const RaceInfo& race = races[static_cast<size_t>(
                std::clamp(m_selectedRace, 0, static_cast<i32>(races.size()) - 1))];

            y += ui.Paragraph({ innerX, y, innerW, 0.0f }, race.description, theme.textDim);

            y += 10.0f;
            const RaceModifiers& mods = race.modifiers;
            auto modifier = [&](const std::string& label, f32 value)
            {
                const Color color = value > 1.001f ? theme.positive
                                  : (value < 0.999f ? theme.negative : theme.text);
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%+.0f%%", (value - 1.0f) * 100.0f);
                ui.KeyValue({ innerX, y, innerW, 20.0f }, label, buffer, color);
                y += 21.0f;
            };
            modifier("Приріст населення", mods.populationGrowth);
            modifier("Їжа", mods.foodProduction);
            modifier("Срібло", mods.moneyProduction);
            modifier("Дерево", mods.woodProduction);
            modifier("Камінь", mods.stoneProduction);
            modifier("Покриття", mods.coverage);
            modifier("Вірність", mods.loyalty);
            modifier("Ціна найму", mods.recruitCost);
        }
    }

    void PartySetupScene::RenderColors(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        ui.Panel(area, "Прапори та зведення");

        const Rect body = Rect{ area.x, area.y + theme.headerHeight, area.w, area.h - theme.headerHeight }
                              .Inset(theme.padding);

        const f32 rowHeight = 26.0f;
        const size_t swatchRows = (m_palette.size() + 5) / 6;
        const f32 contentHeight = 150.0f + (m_settings.rivalColors.size() + 1) * rowHeight +
                                  swatchRows * 34.0f + 170.0f;

        const Rect content = ui.BeginScroll(body, contentHeight, m_colorScroll);
        f32 y = content.y;
        const f32 innerW = content.w;

        ui.Label({ content.x, y, innerW, 20.0f }, "ЧИЙ ПРАПОР ЗМІНЮЄМО", theme.accent);
        y += 24.0f;

        // Target rows: the player first, then every rival realm.
        {
            const Rect row{ content.x, y, innerW, rowHeight - 2.0f };
            if (ui.ListItem(row, "Ваш прапор", m_colorTarget == -1)) m_colorTarget = -1;
            renderer.UIRect({ row.Right() - 26.0f, row.y + 4.0f, 22.0f, row.h - 8.0f },
                            Color::FromRGB(m_settings.playerColor));
            y += rowHeight;
        }
        for (size_t i = 0; i < m_settings.rivalColors.size(); ++i)
        {
            const Rect row{ content.x, y, innerW, rowHeight - 2.0f };
            if (ui.ListItem(row, "Суперник " + std::to_string(i + 1), m_colorTarget == static_cast<i32>(i)))
            {
                m_colorTarget = static_cast<i32>(i);
            }
            renderer.UIRect({ row.Right() - 26.0f, row.y + 4.0f, 22.0f, row.h - 8.0f },
                            Color::FromRGB(m_settings.rivalColors[i]));
            y += rowHeight;
        }

        y += 10.0f;
        ui.Label({ content.x, y, innerW, 20.0f }, "ПАЛІТРА", theme.accent);
        y += 24.0f;

        // Palette grid: click a colour to paint the selected banner with it.
        const f32 swatch = 30.0f;
        const f32 gap = 4.0f;
        for (size_t i = 0; i < m_palette.size(); ++i)
        {
            const size_t column = i % 6;
            const size_t row = i / 6;
            const Rect box{ content.x + column * (swatch + gap), y + row * (swatch + gap), swatch, swatch };

            const u32 current = m_colorTarget < 0
                ? m_settings.playerColor
                : m_settings.rivalColors[static_cast<size_t>(m_colorTarget)];

            if (ui.ColorSwatch(box, Color::FromRGB(m_palette[i]), m_palette[i] == current,
                               "swatch" + std::to_string(i)))
            {
                if (m_colorTarget < 0) m_settings.playerColor = m_palette[i];
                else m_settings.rivalColors[static_cast<size_t>(m_colorTarget)] = m_palette[i];
            }
        }
        y += swatchRows * (swatch + gap) + 8.0f;

        if (ui.Button({ content.x, y, innerW, 28.0f }, "Випадкові кольори")) RandomiseColors();
        y += 38.0f;

        renderer.UIRect({ content.x, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        // --- summary --------------------------------------------------------------------------
        ui.Label({ content.x, y, innerW, 20.0f }, "ЗВЕДЕННЯ", theme.accent);
        y += 24.0f;

        auto row = [&](const std::string& key, const std::string& value)
        {
            ui.KeyValue({ content.x, y, innerW, 22.0f }, key, value, theme.text);
            y += 23.0f;
        };

        if (!m_maps.empty())
        {
            const MapDescription& map = m_maps[static_cast<size_t>(
                std::clamp(m_selectedMap, 0, static_cast<i32>(m_maps.size()) - 1))];
            row("Карта", map.name);
            row("Розмір", std::to_string(map.width) + " x " + std::to_string(map.height));
        }
        row("Держав", std::to_string(m_settings.stateCount));
        row("Зерно", m_seedText == "0" ? "випадкове" : m_seedText);

        const std::vector<RaceInfo>& races = RaceDatabase::Get().Races();
        if (!races.empty())
        {
            row("Народ", races[static_cast<size_t>(
                std::clamp(m_selectedRace, 0, static_cast<i32>(races.size()) - 1))].name);
        }
        row("Місце на карті", "випадкове");

        ui.EndScroll();
    }
}
