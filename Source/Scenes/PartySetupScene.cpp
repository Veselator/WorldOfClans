#include "PartySetupScene.h"
#include "MapGenPanel.h"
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
            // No authored maps is not a dead end any more: the generator can make one.
            m_error = "У теці Maps немає жодної карти — світ буде згенеровано";
            m_settings.generateMap = true;
        }
        else
        {
            m_selectedMap = 0;
            m_settings.mapFolder = m_maps[0].folder;
        }

        // Whatever the player last shaped - here, in the lobby or in the editor - is what
        // the generator offers him now.
        m_settings.mapGen = MapGenerator::Shared();
        m_genPanel.SyncText(m_settings.mapGen);

        LoadThumbnails();

        m_palette.clear();
        for (const Json& entry : ConfigManager::Get().Game()["clanColors"].AsArray())
        {
            m_palette.push_back(static_cast<u32>(std::strtoul(entry.AsString("c8452d").c_str(), nullptr, 16)));
        }
        if (m_palette.empty()) m_palette.push_back(0xC8452D);

        m_settings.stateCount = 5;
        m_settings.humanSeat = 0;
        m_settings.seats.clear();
        EnsureSeats();
        RandomiseColors();
    }

    void PartySetupScene::EnsureSeats()
    {
        const size_t wanted = static_cast<size_t>(std::max(2, m_settings.stateCount));
        while (m_settings.seats.size() < wanted)
        {
            SeatSettings seat;
            seat.color = m_palette[m_settings.seats.size() % m_palette.size()];
            m_settings.seats.push_back(seat);
        }
        m_settings.seats.resize(wanted);

        // Exactly one seat is the player's, whatever the list has been dragged through.
        m_settings.humanSeat = std::clamp(m_settings.humanSeat, 0, static_cast<i32>(wanted) - 1);
        for (size_t i = 0; i < m_settings.seats.size(); ++i)
        {
            m_settings.seats[i].human = static_cast<i32>(i) == m_settings.humanSeat;
        }
        m_selectedSeat = std::clamp(m_selectedSeat, 0, static_cast<i32>(wanted) - 1);
    }

    void PartySetupScene::RandomiseColors()
    {
        EnsureSeats();

        // Deal distinct colours out of the palette so no two banners collide.
        std::vector<u32> pool = m_palette;
        Random random;
        random.Shuffle(pool);

        for (size_t i = 0; i < m_settings.seats.size(); ++i)
        {
            m_settings.seats[i].color = pool[i % pool.size()];
        }
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

        EnsureSeats();

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
        RenderSeats({ margin * 3.0f + columnWidth * 2.0f - slideC, top, columnWidth, bottom - top });

        // --- footer --------------------------------------------------------------------------
        const Rect back{ margin, viewport.y - 54.0f, 180.0f, 34.0f };
        if (ui.Button(back, "Назад"))
        {
            SceneManager::Get().RequestBack();
        }

        const bool ready = m_settings.generateMap || !m_maps.empty();
        const Rect start{ viewport.x - margin - 240.0f, viewport.y - 54.0f, 240.0f, 34.0f };
        if (ui.Button(start, "Почати партію", ready))
        {
            m_settings.seed = static_cast<u32>(std::strtoul(m_seedText.c_str(), nullptr, 10));

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

    void PartySetupScene::LoadThumbnails()
    {
        ReleaseThumbnails();
        m_thumbnails.assign(m_maps.size(), 0);

        for (size_t i = 0; i < m_maps.size(); ++i)
        {
            ImageData portrait;
            if (!MapLoader::EnsureMinimap(m_maps[i].folder, portrait)) continue;
            m_thumbnails[i] = Renderer::Get().CreateUITexture(portrait.pixels, portrait.width, portrait.height);
        }
    }

    void PartySetupScene::ReleaseThumbnails()
    {
        for (u32 handle : m_thumbnails) Renderer::Get().ReleaseUITexture(handle);
        m_thumbnails.clear();
    }

    void PartySetupScene::OnExit()
    {
        ReleaseThumbnails();
    }

    void PartySetupScene::RenderMapList(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        ui.Panel(area, "Карта");
        const Rect body = Rect{ area.x, area.y + theme.headerHeight, area.w, area.h - theme.headerHeight }
                              .Inset(theme.padding);

        // Tall rows, because each one carries the map's portrait: a name and a pixel count
        // say nothing about whether a world is an archipelago or one great plain.
        const f32 rowHeight = 92.0f;
        const f32 generatorHeight = m_settings.generateMap ? 780.0f : 56.0f;
        const Rect content = ui.BeginScroll(body, m_maps.size() * rowHeight + generatorHeight + 20.0f,
                                            m_mapScroll);
        f32 y = content.y;

        // --- fresh ground -------------------------------------------------------------------
        {
            const Rect row{ content.x, y, content.w, 44.0f };
            if (ui.ListItem(row, "Згенерувати карту", m_settings.generateMap, theme.accent))
            {
                m_settings.generateMap = true;
            }
            ui.TooltipIfHovered(row,
                "Світ буде створено наново за наведеними нижче умовами.\n"
                "Готову карту буде записано до теки Maps, тож її можна буде\n"
                "відкрити в редакторі, завантажити зі збереження й надіслати іншим гравцям.");
            y += 50.0f;
        }

        if (m_settings.generateMap)
        {
            // The very same block the map editor draws: one generator, one dialogue, and
            // one set of numbers behind both of them.
            y = MapGenPanel::Draw(ui, content.x, y, content.w, m_settings.mapGen,
                                  m_genPanel, m_settings.stateCount);
            MapGenerator::Shared() = m_settings.mapGen;
        }

        if (!m_maps.empty())
        {
            const Rect divider{ content.x, y, content.w, 1.0f };
            renderer.UIRect(divider, theme.border);
            y += 10.0f;
        }

        for (size_t i = 0; i < m_maps.size(); ++i)
        {
            const Rect row{ content.x, y, content.w, rowHeight - 6.0f };
            y += rowHeight;

            const bool selected = !m_settings.generateMap && static_cast<i32>(i) == m_selectedMap;
            if (ui.ListItem(row, "", selected))
            {
                m_selectedMap = static_cast<i32>(i);
                m_settings.mapFolder = m_maps[i].folder;
                m_settings.generateMap = false;
            }

            // The portrait keeps the map's own proportions inside a fixed frame.
            const Rect frame{ row.x + 6.0f, row.y + 6.0f, 118.0f, row.h - 12.0f };
            renderer.UIRect(frame, theme.panelAlt);
            const u32 thumbnail = i < m_thumbnails.size() ? m_thumbnails[i] : 0;
            if (thumbnail != 0 && m_maps[i].width > 0 && m_maps[i].height > 0)
            {
                const f32 want = static_cast<f32>(m_maps[i].width) / static_cast<f32>(m_maps[i].height);
                f32 w = frame.w;
                f32 h = w / want;
                if (h > frame.h) { h = frame.h; w = h * want; }
                renderer.UIImage(thumbnail,
                                 { frame.x + (frame.w - w) * 0.5f, frame.y + (frame.h - h) * 0.5f, w, h },
                                 Color(1.0f, 1.0f, 1.0f, 1.0f));
            }

            const f32 textX = frame.Right() + 10.0f;
            renderer.UIText(m_maps[i].name, { textX, row.y + 12.0f },
                            selected ? theme.accent : theme.textStrong);
            const std::string size = std::to_string(m_maps[i].width) + "x" + std::to_string(m_maps[i].height);
            renderer.UIText(size, { textX, row.y + 34.0f }, theme.textDim, 0.9f);

            if (!m_maps[i].description.empty()) ui.TooltipIfHovered(row, m_maps[i].description);
        }

        ui.EndScroll(y);
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

        ui.Label({ innerX, y, 120.0f, 26.0f }, "Зерно (seed)", theme.textDim);
        ui.TextField({ innerX + 130.0f, y, innerW - 130.0f, 26.0f }, "seed", m_seedText, 10);
        y += 30.0f;
        ui.Label({ innerX, y, innerW, 18.0f }, "0 — випадкове зерно світу", theme.textDim);
        y += 26.0f;

        const Rect fogRow{ innerX, y, innerW, 26.0f };
        ui.Toggle(fogRow, "Туман війни", m_settings.fogOfWar);
        ui.TooltipIfHovered(fogRow,
            "Видно лише те, що бачать ваші люди.\n"
            "Пройдена земля лишається такою, якою ви її бачили востаннє,\n"
            "а куди ви не заходили - там самий туман.");
        y += 34.0f;

        // --- the robbers ------------------------------------------------------------------
        {
            const Rect r{ innerX, y, innerW, 26.0f };
            y += 30.0f;
            ui.Toggle(r, "Розбійники", m_settings.bandits.enabled);
            ui.TooltipIfHovered(r,
                "У глухих кутках карти стають табори розбійників, а з них виходять ватаги.\n"
                "Вони нічого не захоплюють — лише грабують, чиє б село не було, і тягнуть\n"
                "здобич до себе. Спаліть табір — і все закопане під ним ваше.\n\n"
                "Табори щоразу стають наново, тож двічі однаково не буде.");
        }

        if (m_settings.bandits.enabled)
        {
            ui.Slider({ innerX, y, innerW, 26.0f }, "Скільки їх", m_settings.bandits.density, 0.0f, 1.0f);
            y += 28.0f;

            char density[48];
            std::snprintf(density, sizeof(density), "%.0f%%", m_settings.bandits.density * 100.0f);
            ui.LabelRight({ innerX, y, innerW, 16.0f }, density, theme.textDim);
            y += 22.0f;
        }

        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        // --- the people of the seat being edited ------------------------------------------
        const SeatSettings& seat = m_settings.seats[static_cast<size_t>(m_selectedSeat)];
        const std::vector<RaceInfo>& races = RaceDatabase::Get().Races();

        const std::string who = m_selectedSeat == m_settings.humanSeat
            ? std::string("ВАШ НАРОД")
            : "НАРОД: ДЕРЖАВА " + std::to_string(m_selectedSeat + 1);
        ui.Label({ innerX, y, innerW, 20.0f }, who, theme.accent);
        y += 24.0f;

        {
            const Rect row{ innerX, y, innerW, 30.0f };
            if (ui.ListItem(row, "Випадковий", seat.raceId.empty()))
            {
                m_settings.seats[static_cast<size_t>(m_selectedSeat)].raceId.clear();
            }
            ui.TooltipIfHovered(row, "Народ буде обрано жеребом, коли почнеться партія.");
            y += 34.0f;
        }

        for (size_t i = 0; i < races.size(); ++i)
        {
            const Rect row{ innerX, y, innerW, 40.0f };
            const bool selected = seat.raceId == races[i].id;
            if (ui.ListItem(row, races[i].name, selected, races[i].color))
            {
                m_settings.seats[static_cast<size_t>(m_selectedSeat)].raceId = races[i].id;
            }
            renderer.UISprite(races[i].sprite, { row.Right() - 34.0f, row.y + 4.0f, 32.0f, 32.0f },
                              races[i].color);
            ui.TooltipIfHovered(row, races[i].description);
            y += 44.0f;
        }

        y += 8.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        // The race blurb and its modifiers: the actual reason to pick one.
        const RaceInfo* shown = nullptr;
        for (const RaceInfo& race : races)
        {
            if (race.id == seat.raceId) { shown = &race; break; }
        }

        if (!shown)
        {
            ui.Paragraph({ innerX, y, innerW, 0.0f },
                         "Народ цієї держави визначиться жеребом на початку партії.",
                         theme.textDim);
            return;
        }

        y += ui.Paragraph({ innerX, y, innerW, 0.0f }, shown->description, theme.textDim);

        y += 10.0f;
        const RaceModifiers& mods = shown->modifiers;
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

    void PartySetupScene::RenderSeats(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        ui.Panel(area, "Держави та прапори");

        const Rect body = Rect{ area.x, area.y + theme.headerHeight, area.w, area.h - theme.headerHeight }
                              .Inset(theme.padding);

        const Rect content = ui.BeginScroll(body, body.h, m_seatScroll);
        f32 y = content.y;
        const f32 innerW = content.w;

        ui.Label({ content.x, y, innerW, 20.0f }, "ЧИЮ ДЕРЖАВУ НАЛАШТОВУЄМО", theme.accent);
        y += 24.0f;

        const RaceDatabase& races = RaceDatabase::Get();
        const f32 rowHeight = 30.0f;

        for (size_t i = 0; i < m_settings.seats.size(); ++i)
        {
            const SeatSettings& seat = m_settings.seats[i];
            const Rect row{ content.x, y, innerW, rowHeight - 2.0f };
            y += rowHeight;

            const bool isPlayer = static_cast<i32>(i) == m_settings.humanSeat;
            const std::string label = (isPlayer ? "Ви — держава " : "Держава ") + std::to_string(i + 1);
            if (ui.ListItem(row, label, static_cast<i32>(i) == m_selectedSeat))
            {
                m_selectedSeat = static_cast<i32>(i);
            }

            // The people on the left of the swatch, so the list reads as a table of realms.
            const std::string people = seat.raceId.empty() ? "випадковий" : races.Race(seat.raceId).name;
            ui.LabelRight({ row.x, row.y, row.w - 30.0f, row.h }, people, theme.textDim);
            renderer.UIRect({ row.Right() - 26.0f, row.y + 4.0f, 22.0f, row.h - 8.0f },
                            Color::FromRGB(seat.color));
        }

        y += 8.0f;
        {
            const Rect r{ content.x, y, innerW, 26.0f };
            y += 32.0f;
            const bool already = m_selectedSeat == m_settings.humanSeat;
            if (ui.Button(r, "Грати за цю державу", !already))
            {
                m_settings.humanSeat = m_selectedSeat;
            }
            ui.TooltipIfHovered(r, already
                ? "Ви вже граєте за неї."
                : "Ви сядете за цю державу, а решта дістанеться машині.");
        }

        y += 4.0f;
        ui.Label({ content.x, y, innerW, 20.0f }, "ПАЛІТРА", theme.accent);
        y += 24.0f;

        // Palette grid: click a colour to paint the selected banner with it.
        const f32 swatch = 30.0f;
        const f32 gap = 4.0f;
        const size_t swatchRows = (m_palette.size() + 5) / 6;
        for (size_t i = 0; i < m_palette.size(); ++i)
        {
            const size_t column = i % 6;
            const size_t row = i / 6;
            const Rect box{ content.x + column * (swatch + gap), y + row * (swatch + gap), swatch, swatch };

            const u32 current = m_settings.seats[static_cast<size_t>(m_selectedSeat)].color;
            if (ui.ColorSwatch(box, Color::FromRGB(m_palette[i]), m_palette[i] == current,
                               "swatch" + std::to_string(i)))
            {
                m_settings.seats[static_cast<size_t>(m_selectedSeat)].color = m_palette[i];
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

        if (m_settings.generateMap)
        {
            row("Карта", "буде згенеровано");
            row("Розмір", std::to_string(m_settings.mapGen.width) + " x " +
                          std::to_string(m_settings.mapGen.height));
            row("Зерно карти", m_settings.mapGen.seed == 0 ? "випадкове"
                                                             : std::to_string(m_settings.mapGen.seed));
        }
        else if (!m_maps.empty())
        {
            const MapDescription& map = m_maps[static_cast<size_t>(
                std::clamp(m_selectedMap, 0, static_cast<i32>(m_maps.size()) - 1))];
            row("Карта", map.name);
            row("Розмір", std::to_string(map.width) + " x " + std::to_string(map.height));
        }
        row("Держав", std::to_string(m_settings.stateCount));
        row("Зерно світу", m_seedText == "0" ? "випадкове" : m_seedText);
        row("Місце на карті", "випадкове");

        ui.EndScroll(y);
    }
}
