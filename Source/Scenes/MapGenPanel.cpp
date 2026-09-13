#include "MapGenPanel.h"
#include "../Core/Config.h"
#include "../Render/Renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace woc
{
    void MapGenPanelState::SyncText(const MapGenSettings& settings)
    {
        seedText = std::to_string(settings.seed);
        widthText = std::to_string(settings.width);
        heightText = std::to_string(settings.height);
    }

    f32 MapGenPanel::Draw(UI& ui, f32 x, f32 y, f32 width, MapGenSettings& settings,
                          MapGenPanelState& state, i32 realmCount, bool includeSize)
    {
        const Theme& theme = Theme::Get();

        auto row = [&](f32 height)
        {
            const Rect r{ x, y, width, height };
            y += height + 6.0f;
            return r;
        };

        // The presets are the honest front door to a generator with a dozen knobs on it:
        // pick the world you want, then nudge it. Each one leaves the size, the seed and
        // the realm count alone, because those are the player's, not the preset's.
        ui.Label(row(20.0f), "ЯКИЙ СВІТ", theme.accent);
        const std::vector<MapGenPreset>& presets = MapGenerator::Presets();
        for (size_t i = 0; i < presets.size(); ++i)
        {
            const Rect r = row(26.0f);
            if (ui.ListItem(r, presets[i].name, static_cast<i32>(i) == state.preset))
            {
                state.preset = static_cast<i32>(i);

                const u32 keptWidth = settings.width;
                const u32 keptHeight = settings.height;
                const u32 keptSeed = settings.seed;
                settings = presets[i].settings;
                settings.width = keptWidth;
                settings.height = keptHeight;
                settings.seed = keptSeed;
            }
            ui.TooltipIfHovered(r, presets[i].description);
        }

        if (state.preset >= 0 && state.preset < static_cast<i32>(presets.size()))
        {
            y += ui.Paragraph({ x, y, width, 0.0f },
                              presets[static_cast<size_t>(state.preset)].description,
                              theme.textDim) + 8.0f;
        }

        ui.Label(row(20.0f), "УМОВИ СВІТУ", theme.accent);

        ui.Label(row(18.0f), "Зерно карти", theme.textDim);
        if (ui.TextField(row(26.0f), "mapGenSeed", state.seedText, 10))
        {
            settings.seed = static_cast<u32>(std::max(0, std::atoi(state.seedText.c_str())));
        }
        ui.Label(row(16.0f), "0 — випадкове", theme.textDim);

        if (includeSize)
        {
            // A map is not resized by a pixel, so the buttons move it by a whole step - and
            // the field beside them takes any figure at all, for somebody who knows exactly
            // what he wants.
            const i32 step = ConfigManager::Get().Int("map/generatorSizeStep", 128);

            auto dimension = [&](const char* label, const char* id, u32& value, std::string& text)
            {
                const Rect r = row(26.0f);
                const f32 buttonWidth = 26.0f;
                const f32 fieldWidth = 90.0f;

                ui.Label({ r.x, r.y, r.w, r.h }, label, theme.textDim);

                const f32 right = r.Right();
                if (ui.Button({ right - fieldWidth - buttonWidth * 2.0f - 8.0f, r.y,
                                buttonWidth, r.h }, "-", value > 512))
                {
                    value = static_cast<u32>(std::max(512, static_cast<i32>(value) - step));
                    text = std::to_string(value);
                }
                if (ui.Button({ right - fieldWidth - buttonWidth - 4.0f, r.y, buttonWidth, r.h },
                              "+", value < 8192))
                {
                    value = static_cast<u32>(std::min(8192, static_cast<i32>(value) + step));
                    text = std::to_string(value);
                }

                if (ui.TextField({ right - fieldWidth, r.y, fieldWidth, r.h }, id, text, 5))
                {
                    const i32 typed = std::atoi(text.c_str());
                    if (typed >= 512) value = static_cast<u32>(std::min(8192, typed));
                }
            };

            dimension("Ширина", "mapGenWidth", settings.width, state.widthText);
            dimension("Висота", "mapGenHeight", settings.height, state.heightText);
        }

        auto knob = [&](const char* label, f32& value, f32 low, f32 high, const char* hint)
        {
            const Rect r = row(26.0f);
            ui.Slider(r, label, value, low, high);
            ui.TooltipIfHovered(r, hint);
        };

        knob("Дрібність", settings.scale, 1.5f, 6.0f,
             "Скільки всього вміщається на карту. Менше — кілька великих країв;\n"
             "більше — дрібна, строката, порізана земля.");
        knob("Суходіл", settings.landMass, 0.2f, 0.95f,
             "Скільки взагалі землі проти води.");
        knob("Суцільність", settings.continent, 0.0f, 1.0f,
             "Нуль розсипає землю на архіпелаг; одиниця збирає її в один материк.");
        knob("Порізаність берега", settings.coastRoughness, 0.0f, 1.2f,
             "Наскільки берегова лінія покручена: затоки, миси, фіорди.");
        knob("Хребти", settings.ridges, 0.0f, 1.0f,
             "Нуль дає окремі горби; одиниця — довгі гірські пасма з перевалами.");
        knob("Рівень моря", settings.seaLevel, 0.25f, 0.6f,
             "Де проходить уріз води.");
        knob("Висота гір", settings.mountains, 0.55f, 0.95f,
             "З якої висоти земля стає пагорбами, високогір'ям і неприступними горами.");
        knob("Ліси", settings.forest, 0.0f, 0.9f, "Скільки лісу на рівнинах.");
        knob("Посуха", settings.aridity, 0.0f, 1.0f,
             "Суха земля втрачає ліси, чорнозем робиться суглинком,\n"
             "а найгірше — піском.");

        {
            const Rect r = row(24.0f);
            i32 detail = settings.octaves;
            if (ui.Stepper(r, "Подрібнення", detail, 2, 8)) settings.octaves = detail;
            ui.TooltipIfHovered(r, "Скільки шарів шуму лягає на висоту: більше — "
                                   "дрібніший рельєф і більше деталей.");
        }

        {
            const Rect r = row(26.0f);
            bool perRealm = settings.islands >= 2;
            if (ui.Toggle(r, "Кожній державі — свій острів", perRealm))
            {
                settings.islands = perRealm ? std::max(2, realmCount) : 0;
            }
            ui.TooltipIfHovered(r,
                "Замість одного суходолу — по острову на державу, сполучені "
                "вузькими перешийками.");
        }

        if (settings.islands >= 2)
        {
            // The island count follows the realm count: one each is the whole idea.
            settings.islands = std::max(2, realmCount);
            knob("Ширина перешийків", settings.isthmusWidth, 0.0f, 80.0f,
                 "Наскільки широкі смуги землі між островами. Нуль лишає острови "
                 "розділеними зовсім.");
        }

        char summary[96];
        std::snprintf(summary, sizeof(summary), "%u x %u", settings.width, settings.height);
        ui.LabelRight(row(18.0f), summary, theme.textDim);

        return y;
    }
}
