// EditorSceneUI.cpp - the editor's toolbar and inspector.
#include "EditorScene.h"
#include "SceneManager.h"

#include "../Core/Config.h"
#include "../Game/Systems/CoverageSystem.h"
#include "../Game/World/RaceDatabase.h"
#include "../Game/World/World.h"
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
        const char* ToolName(EditorTool tool)
        {
            switch (tool)
            {
            case EditorTool::Terrain:    return "Ландшафт";
            case EditorTool::Height:     return "Висота";
            case EditorTool::Forest:     return "Ліс";
            case EditorTool::Field:      return "Поля";
            case EditorTool::Road:       return "Дороги";
            case EditorTool::Settlement: return "Поселення";
            case EditorTool::Mine:       return "Шахти";
            case EditorTool::Erase:      return "Видалити";
            case EditorTool::Inspect:    return "Огляд";
            }
            return "?";
        }

        const char* ToolHint(EditorTool tool)
        {
            switch (tool)
            {
            case EditorTool::Terrain:    return "ЛКМ — замалювати вибраним типом місцевості";
            case EditorTool::Height:     return "ЛКМ — підняти ґрунт, ПКМ — опустити (або вирівняти до заданої висоти)";
            case EditorTool::Forest:     return "ЛКМ — засадити ліс, ПКМ — вирубати";
            case EditorTool::Field:      return "ЛКМ — зорати, ПКМ — закинути";
            case EditorTool::Road:       return "ЛКМ — прокласти дорогу, ПКМ — зняти";
            case EditorTool::Settlement: return "ЛКМ — поставити поселення, ПКМ — знести";
            case EditorTool::Mine:       return "ЛКМ — поставити шахту, ПКМ — знести";
            case EditorTool::Erase:      return "ЛКМ — видалити об'єкт під курсором";
            case EditorTool::Inspect:    return "ЛКМ — вибрати об'єкт і переглянути його дані";
            }
            return "";
        }
    }

    void EditorScene::DrawToolbar()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();

        const Vec2 viewport = renderer.ViewportSize();

        // --- top strip ----------------------------------------------------------------------
        const Rect bar{ 0.0f, 0.0f, viewport.x, theme.topBarHeight };
        renderer.UIRect(bar, theme.panelHeader.WithAlpha(0.97f));
        renderer.UIRect({ 0.0f, bar.Bottom() - 1.0f, viewport.x, 1.0f }, theme.border);
        ui.BlockMouse(bar);

        renderer.UIText("РЕДАКТОР КАРТ", { 12.0f, (theme.topBarHeight - renderer.TextHeight()) * 0.5f },
                        theme.accent);

        const Rect menuButton{ viewport.x - 120.0f, 3.0f, 110.0f, theme.topBarHeight - 6.0f };
        if (ui.Button(menuButton, "У меню")) SceneManager::Get().Request(SceneId::MainMenu);

        const Rect saveButton{ menuButton.x - 120.0f, 3.0f, 110.0f, theme.topBarHeight - 6.0f };
        if (ui.Button(saveButton, "Зберегти")) SaveMap();

        renderer.UIText(ToolHint(m_tool),
                        { 200.0f, (theme.topBarHeight - renderer.TextHeight()) * 0.5f }, theme.textDim);

        // --- left panel ------------------------------------------------------------------------
        const f32 panelWidth = 260.0f;
        const Rect panel{ 0.0f, theme.topBarHeight, panelWidth, viewport.y - theme.topBarHeight };
        ui.Panel(panel, "Інструменти");

        f32 y = panel.y + theme.headerHeight + theme.padding;
        const f32 innerX = panel.x + theme.padding;
        const f32 innerW = panel.w - theme.padding * 2.0f;

        auto row = [&](f32 height)
        {
            const Rect r{ innerX, y, innerW, height };
            y += height + 4.0f;
            return r;
        };

        // Tool picker, two per line.
        for (int i = 0; i < kEditorToolCount; i += 2)
        {
            const Rect line = row(26.0f);
            const Rect left{ line.x, line.y, line.w * 0.5f - 3.0f, line.h };
            const Rect right{ line.x + line.w * 0.5f + 3.0f, line.y, line.w * 0.5f - 3.0f, line.h };

            const EditorTool a = static_cast<EditorTool>(i);
            if (ui.ListItem(left, ToolName(a), m_tool == a)) m_tool = a;

            if (i + 1 >= kEditorToolCount) continue;
            const EditorTool b = static_cast<EditorTool>(i + 1);
            if (ui.ListItem(right, ToolName(b), m_tool == b)) m_tool = b;
        }

        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 8.0f;

        // --- brush -------------------------------------------------------------------------------
        if (m_tool == EditorTool::Terrain || m_tool == EditorTool::Height ||
            m_tool == EditorTool::Forest || m_tool == EditorTool::Field ||
            m_tool == EditorTool::Road)
        {
            ui.Stepper(row(24.0f), "Радіус", m_brushRadius, 4, 200);
            ui.Slider(row(24.0f), "Сила", m_brushStrength, 0.05f, 1.0f);
        }

        if (m_tool == EditorTool::Height)
        {
            ui.Label(row(20.0f), "ЯК ПРАЦЮЄ", theme.accent);

            const Rect sculpt = row(22.0f);
            if (ui.ListItem(sculpt, "Ліпити", m_heightMode == HeightMode::Sculpt))
            {
                m_heightMode = HeightMode::Sculpt;
            }
            ui.TooltipIfHovered(sculpt, "ЛКМ піднімає ґрунт, ПКМ опускає — поступово, із заданою силою.");

            const Rect level = row(22.0f);
            if (ui.ListItem(level, "Вирівняти", m_heightMode == HeightMode::Level))
            {
                m_heightMode = HeightMode::Level;
            }
            ui.TooltipIfHovered(level, "Тягне ґрунт до заданої висоти — для плато, долин і озерних чаш.");

            if (m_heightMode == HeightMode::Level)
            {
                ui.Slider(row(24.0f), "Висота", m_heightTarget, 0.0f, 1.0f);
            }
        }

        if (m_tool == EditorTool::Terrain)
        {
            ui.Label(row(20.0f), "ТИП МІСЦЕВОСТІ", theme.accent);
            const std::vector<TerrainInfo>& types = TerrainDatabase::Get().All();
            for (size_t i = 0; i < types.size(); ++i)
            {
                const Rect r = row(22.0f);
                const bool selected = static_cast<i32>(i) == m_terrainIndex;
                if (ui.ListItem(r, types[i].name, selected, Color::FromRGB(types[i].color)))
                {
                    m_terrainIndex = static_cast<i32>(i);
                }
                renderer.UIRect({ r.Right() - 20.0f, r.y + 4.0f, 16.0f, r.h - 8.0f },
                                Color::FromRGB(types[i].color));
            }
        }

        if (m_tool == EditorTool::Settlement)
        {
            ui.Label(row(20.0f), "ЩО СТАВИМО", theme.accent);
            const char* kindNames[] = { "Село", "Місто", "Замок" };
            for (int i = 0; i < 3; ++i)
            {
                const Rect r = row(22.0f);
                if (ui.ListItem(r, kindNames[i], static_cast<i32>(m_settlementKind) == i))
                {
                    m_settlementKind = static_cast<SettlementKind>(i);
                }
            }

            ui.Label(row(20.0f), "НАРОД", theme.accent);
            const std::vector<RaceInfo>& races = RaceDatabase::Get().Races();
            for (size_t i = 0; i < races.size(); ++i)
            {
                const Rect r = row(22.0f);
                if (ui.ListItem(r, races[i].name, static_cast<i32>(i) == m_raceIndex, races[i].color))
                {
                    m_raceIndex = static_cast<i32>(i);
                }
            }

            ui.Stepper(row(24.0f), "Власник", m_ownerSlot, 0, 12);
            ui.Label(row(18.0f), m_ownerSlot == 0 ? "0 — незалежне" : "номер держави", theme.textDim);
        }

        // --- generation ------------------------------------------------------------------------------
        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 8.0f;
        ui.Label(row(20.0f), "ГЕНЕРАЦІЯ", theme.accent);

        // Either the designer names a seed, or the generator draws one and writes it back
        // into the field, so a world that turns out well can always be found again.
        const Rect randomRow = row(24.0f);
        ui.Toggle(randomRow, "Випадкове зерно", m_randomSeed);
        ui.TooltipIfHovered(randomRow, "Зерно вибереться саме, і його буде вписано в поле нижче.");

        ui.Label(row(18.0f), "Зерно", theme.textDim);
        if (m_randomSeed)
        {
            const Rect r = row(24.0f);
            renderer.UIRect(r, theme.panelAlt.Scaled(0.8f));
            renderer.UIRectOutline(r, theme.border, 1.0f);
            renderer.UIText(m_seedText, { r.x + 6.0f, r.y + (r.h - renderer.TextHeight()) * 0.5f },
                            theme.textDim);
        }
        else if (ui.TextField(row(24.0f), "genSeed", m_seedText, 10))
        {
            m_genSeed = std::max(1, std::atoi(m_seedText.c_str()));
        }
        ui.Slider(row(24.0f), "Масштаб", m_genScale, 1.0f, 8.0f);
        ui.Slider(row(24.0f), "Рівень моря", m_genSeaLevel, 0.2f, 0.7f);
        ui.Slider(row(24.0f), "Гори", m_genMountains, 0.55f, 0.95f);
        ui.Slider(row(24.0f), "Ліси", m_genForest, 0.0f, 1.0f);

        if (ui.Button(row(28.0f), "Згенерувати ландшафт")) GenerateTerrain();

        // --- the size of the map that is open ----------------------------------------------------
        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 8.0f;
        ui.Label(row(20.0f), "РОЗМІР КАРТИ", theme.accent);

        ui.Stepper(row(24.0f), "Ширина", m_genWidth, 256, 8192);
        ui.Stepper(row(24.0f), "Висота", m_genHeight, 256, 8192);

        {
            const Rect r = row(28.0f);
            const MapData& map = World::Get().Map();
            const bool changed = static_cast<u32>(m_genWidth) != map.PixelWidth() ||
                                 static_cast<u32>(m_genHeight) != map.PixelHeight();
            if (ui.Button(r, "Змінити розмір", changed)) ResizeMap();
            ui.TooltipIfHovered(r, changed
                ? "Намальоване лишиться на місці; зайве обріжеться, нове заллється водою."
                : "Карта вже такого розміру.");
        }

        // --- saving ------------------------------------------------------------------------------------
        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 8.0f;
        if (ui.Button(row(30.0f), "Зберегти карту")) SaveMap();
        ui.Label(row(18.0f), "Maps/" + m_folderName, theme.textDim);

        if (ui.Button(row(28.0f), "Список карт...")) { m_browserOpen = true; m_browserScroll = 0.0f; }
    }

    void EditorScene::DrawInspector()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        World& world = World::Get();

        const Vec2 viewport = renderer.ViewportSize();
        const f32 width = 280.0f;
        const Rect panel{ viewport.x - width, theme.topBarHeight, width, viewport.y - theme.topBarHeight };
        ui.Panel(panel, "Карта");

        f32 y = panel.y + theme.headerHeight + theme.padding;
        const Rect line{ panel.x + theme.padding, y, panel.w - theme.padding * 2.0f, theme.rowHeight };
        auto row = [&]()
        {
            const Rect r{ line.x, y, line.w, theme.rowHeight };
            y += theme.rowHeight + 2.0f;
            return r;
        };

        const MapData& map = world.Map();
        ui.KeyValue(row(), "Назва", m_mapName, theme.text);
        ui.KeyValue(row(), "Розмір", std::to_string(map.PixelWidth()) + " x " +
                    std::to_string(map.PixelHeight()), theme.text);
        ui.KeyValue(row(), "Сітка", std::to_string(map.TileWidth()) + " x " +
                    std::to_string(map.TileHeight()), theme.text);
        ui.KeyValue(row(), "Поселень", std::to_string(world.Settlements().size()), theme.text);
        ui.KeyValue(row(), "Шахт", std::to_string(world.Mines().size()), theme.text);
        ui.KeyValue(row(), "Держав", std::to_string(world.States().size()), theme.text);

        // --- terrain under the cursor ---------------------------------------------------------
        y += 8.0f;
        ui.Label(row(), "ПІД КУРСОРОМ", theme.accent);
        if (!ui.WantsMouse())
        {
            const Vec2 mapPosition = ScreenToTerrain(Input::Get().MousePosition());
            if (mapPosition.x >= 0.0f && mapPosition.y >= 0.0f &&
                mapPosition.x < static_cast<f32>(map.PixelWidth()) &&
                mapPosition.y < static_cast<f32>(map.PixelHeight()))
            {
                const Coord tile = map.ToTile(mapPosition);
                const Tile& data = map.At(tile);
                const TerrainInfo& info = TerrainDatabase::Get().At(data.terrain);

                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "%.0f, %.0f", mapPosition.x, mapPosition.y);
                ui.KeyValue(row(), "Координати", buffer, theme.text);
                ui.KeyValue(row(), "Місцевість", info.name, Color::FromRGB(info.color));

                std::snprintf(buffer, sizeof(buffer), "%.2f", data.height);
                ui.KeyValue(row(), "Висота", buffer, theme.text);
                std::snprintf(buffer, sizeof(buffer), "%.0f%%", data.forest * 100.0f);
                ui.KeyValue(row(), "Ліс", buffer, theme.text);
                std::snprintf(buffer, sizeof(buffer), "%.0f%%", data.field * 100.0f);
                ui.KeyValue(row(), "Поля", buffer, theme.text);
                ui.KeyValue(row(), "Дорога", data.road > 0 ? "є" : "немає", theme.text);
                ui.KeyValue(row(), "Брід", data.fordable ? "так" : "немає", theme.text);
            }
        }

        // --- selected settlement ----------------------------------------------------------------
        if (m_inspected != kInvalidId)
        {
            Settlement* settlement = world.FindSettlement(m_inspected);
            if (settlement)
            {
                y += 8.0f;
                ui.Label(row(), "ВИБРАНИЙ ОБ'ЄКТ", theme.accent);

                const Clan* owner = world.FindClan(settlement->owner);
                ui.KeyValue(row(), "Назва", settlement->name, theme.textStrong);
                ui.KeyValue(row(), "Тип", settlement->KindInfo().name, theme.text);
                ui.KeyValue(row(), "Власник", owner ? owner->name : "незалежне",
                            owner ? owner->color : theme.textDim);
                ui.KeyValue(row(), "Народ", RaceDatabase::Get().Race(settlement->raceId).name, theme.text);
                ui.KeyValue(row(), "Віра", RaceDatabase::Get().Faith(settlement->faithId).name, theme.text);

                i32 hundreds = std::max(1, settlement->population / 100);
                if (ui.Stepper(row(), "Населення x100", hundreds, 1, 200))
                {
                    settlement->population = hundreds * 100;
                }
                ui.Slider(row(), "Процвітання", settlement->prosperity, 0.0f, settlement->ProsperityCeiling());
                ui.Slider(row(), "Вірність", settlement->loyalty, 0.0f, 1.0f);

                y += 4.0f;
                if (ui.Button({ line.x, y, line.w, 26.0f }, "Видалити об'єкт"))
                {
                    world.DestroySettlement(m_inspected);
                    m_inspected = kInvalidId;
                    CoverageSystem::Get().MarkDirty();
                }
                y += 30.0f;
            }
            else
            {
                m_inspected = kInvalidId;
            }
        }

        // --- legend ------------------------------------------------------------------------------------
        y = std::max(y, panel.Bottom() - 96.0f);
        renderer.UIRect({ line.x, y, line.w, 1.0f }, theme.border);
        y += 8.0f;
        renderer.UIText("WASD — камера, Q/E — поворот, R — скинути", { line.x, y }, theme.textDim);
        y += renderer.TextHeight() + 3.0f;
        renderer.UIText("Esc — вийти в меню", { line.x, y }, theme.textDim);
        y += renderer.TextHeight() + 3.0f;
        renderer.UIText("Кордони оновлюються самі", { line.x, y }, theme.textDim);
    }
}
