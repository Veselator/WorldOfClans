#include "SettingsScene.h"
#include "SceneManager.h"

#include "../Audio/AudioSystem.h"
#include "../Core/Settings.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace woc
{
    void SettingsPanel::Open()
    {
        Settings& settings = Settings::Get();
        for (size_t i = 0; i < settings.Resolutions().size(); ++i)
        {
            if (settings.Resolutions()[i].width == settings.windowWidth &&
                settings.Resolutions()[i].height == settings.windowHeight)
            {
                m_resolutionIndex = static_cast<i32>(i);
            }
        }

        m_open = true;
        UI::Get().RestartTransition("menu.settings");
    }

    void SettingsPanel::Close()
    {
        if (!m_open) return;
        m_open = false;
        Settings::Get().Save();
    }

    void SettingsPanel::Update(f32 deltaTime)
    {
        if (m_statusTimer > 0.0f) m_statusTimer -= deltaTime;
        if (!m_open) return;

        if (Input::Get().WasKeyPressed(Key::Escape) && !UI::Get().WantsKeyboard()) Close();
    }

    void SettingsPanel::Draw()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();

        const f32 fade = ui.Transition("menu.settings", m_open, 0.16f);
        if (fade <= 0.001f) return;

        const Vec2 viewport = renderer.ViewportSize();
        renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, theme.shadow.WithAlpha(0.68f * fade));
        ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        // Wide enough for two columns and no wider; on a small window it simply takes what
        // there is, which is what the margins below are computed from.
        const f32 width = std::min(viewport.x - 60.0f, 1020.0f);
        const f32 height = std::min(viewport.y - 60.0f, 700.0f);
        const Rect panel{ (viewport.x - width) * 0.5f,
                          (viewport.y - height) * 0.5f + (1.0f - fade) * 30.0f, width, height };
        ui.Panel(panel, "Налаштування");

        const f32 margin = 16.0f;
        const f32 top = panel.y + theme.headerHeight + margin;
        const f32 bottom = panel.Bottom() - 56.0f;
        const f32 columnWidth = (panel.w - margin * 3.0f) * 0.5f;

        DrawDisplay({ panel.x + margin, top, columnWidth, bottom - top });
        DrawGameplay({ panel.x + margin * 2.0f + columnWidth, top, columnWidth, bottom - top });

        const Rect back{ panel.x + margin, panel.Bottom() - 44.0f, 200.0f, 32.0f };
        if (ui.Button(back, "Назад")) Close();

        if (m_statusTimer > 0.0f && !m_status.empty())
        {
            renderer.UIText(m_status, { back.Right() + 20.0f, back.y + 8.0f }, theme.positive);
        }
    }

    void SettingsPanel::DrawDisplay(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        Settings& settings = Settings::Get();

        ui.Panel(area, "Зображення та звук");

        const f32 innerX = area.x + theme.padding;
        const f32 innerW = area.w - theme.padding * 2.0f;
        f32 y = area.y + theme.headerHeight + theme.padding;

        auto row = [&](f32 height)
        {
            const Rect r{ innerX, y, innerW, height };
            y += height + 6.0f;
            return r;
        };

        bool fullscreen = settings.fullscreen;
        if (ui.Toggle(row(26.0f), "Повний екран", fullscreen))
        {
            settings.fullscreen = fullscreen;
            m_dirty = true;
        }

        ui.Label(row(20.0f), "Роздільність вікна", theme.textDim);
        for (size_t i = 0; i < settings.Resolutions().size(); ++i)
        {
            const Rect r = row(24.0f);
            const bool selected = static_cast<i32>(i) == m_resolutionIndex;
            if (ui.ListItem(r, settings.Resolutions()[i].Label(), selected))
            {
                m_resolutionIndex = static_cast<i32>(i);
                settings.windowWidth = settings.Resolutions()[i].width;
                settings.windowHeight = settings.Resolutions()[i].height;
                m_dirty = true;
            }
        }
        if (settings.fullscreen)
        {
            ui.Label(row(18.0f), "(діє після виходу з повного екрана)", theme.textDim);
        }

        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        // --- sound ---------------------------------------------------------------------------
        // These two take effect under the hand: the menu's own music is playing while the
        // handle moves, and hearing the change is the whole point of the control.
        ui.Label(row(20.0f), "ЗВУК", theme.accent);
        {
            const Rect r = row(26.0f);
            if (ui.Slider(r, "Загальна гучність", settings.masterVolume, 0.0f, 1.0f))
            {
                AudioSystem::Get().SetMasterVolume(settings.masterVolume);
            }
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", settings.masterVolume * 100.0f);
            ui.LabelRight(row(16.0f), buffer, theme.textDim);
        }
        {
            const Rect r = row(26.0f);
            if (ui.Slider(r, "Музика", settings.musicVolume, 0.0f, 1.0f))
            {
                AudioSystem::Get().SetMusicVolume(settings.musicVolume);
            }
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", settings.musicVolume * 100.0f);
            ui.LabelRight(row(16.0f), buffer, theme.textDim);
        }
        {
            const Rect r = row(26.0f);
            if (ui.Slider(r, "Звуки", settings.sfxVolume, 0.0f, 1.0f))
            {
                AudioSystem::Get().SetSfxVolume(settings.sfxVolume);
            }
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", settings.sfxVolume * 100.0f);
            ui.LabelRight(row(16.0f), buffer, theme.textDim);
        }

        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        bool vsync = settings.vsync;
        if (ui.Toggle(row(26.0f), "Вертикальна синхронізація", vsync))
        {
            settings.vsync = vsync;
            m_status = "Синхронізація застосується після перезапуску";
            m_statusTimer = 5.0f;
        }

        y += 6.0f;
        if (ui.Button(row(30.0f), "Застосувати"))
        {
            m_dirty = false;
            m_status = "Застосовано";
            m_statusTimer = 3.0f;
            SceneManager::Get().SetPayload("applySettings", "1");
        }
    }

    void SettingsPanel::DrawGameplay(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        Settings& settings = Settings::Get();

        ui.Panel(area, "Карта, камера та гра");

        const f32 innerX = area.x + theme.padding;
        const f32 innerW = area.w - theme.padding * 2.0f;
        f32 y = area.y + theme.headerHeight + theme.padding;

        auto row = [&](f32 height)
        {
            const Rect r{ innerX, y, innerW, height };
            y += height + 6.0f;
            return r;
        };

        bool smooth = settings.smoothBorders;
        const Rect smoothRow = row(26.0f);
        if (ui.Toggle(smoothRow, "Плавні кордони", smooth))
        {
            settings.smoothBorders = smooth;
            Renderer::Get().SetSmoothBorders(smooth);
        }
        ui.TooltipIfHovered(smoothRow,
            "За звичаєм кордон іде по клітинках, як його й намальовано.\n"
            "Увімкнено — межу згладжено в лінію: гарніше, та трохи дорожче для відеокарти.");

        bool fps = settings.showFps;
        if (ui.Toggle(row(26.0f), "Показувати FPS", fps)) settings.showFps = fps;

        bool labels = settings.showLabels;
        if (ui.Toggle(row(26.0f), "Назви поселень", labels)) settings.showLabels = labels;

        ui.Slider(row(26.0f), "Масштаб для назв", settings.labelMinZoom, 0.5f, 3.5f);

        // The interface's own size. Applied on "Застосувати" rather than as the handle
        // moves: every panel on this very screen is laid out from these metrics, and
        // resizing them mid-frame would drag the slider out from under the cursor.
        ui.Slider(row(26.0f), "Масштаб інтерфейсу", settings.uiScale, 0.75f, 1.75f);
        {
            char scaleText[32];
            std::snprintf(scaleText, sizeof(scaleText), "%.0f%%", settings.uiScale * 100.0f);
            ui.LabelRight(row(16.0f), scaleText, theme.textDim);
        }

        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        // The camera's tilt is not here on purpose: the whole map is drawn around one
        // angle, and the only thing letting it be dragged about achieves is breaking it.
        ui.Slider(row(26.0f), "Швидкість обертання", settings.rotateSpeed, 20.0f, 220.0f);
        ui.Slider(row(26.0f), "Прокрутка від краю", settings.edgeScroll, 0.0f, 30.0f);

        ui.Label(row(20.0f), "Швидкість гри за замовчуванням", theme.textDim);
        ui.Stepper(row(26.0f), "Крок", settings.defaultSpeedIndex, 0, 5);

        // Autosave is quoted in the game's own calendar rather than in minutes: a party at
        // eight times speed and one at a half do not want the same wall-clock interval.
        {
            const std::vector<i32>& choices = Settings::AutosaveChoices();
            i32 index = 0;
            for (size_t i = 0; i < choices.size(); ++i)
            {
                if (choices[i] == settings.autosaveDays) index = static_cast<i32>(i);
            }

            const Rect r = row(26.0f);
            if (ui.Stepper(r, "Автозбереження", index, 0, static_cast<i32>(choices.size()) - 1))
            {
                settings.autosaveDays = choices[static_cast<size_t>(index)];
            }
            ui.LabelRight(row(18.0f), Settings::AutosaveLabel(settings.autosaveDays), theme.textDim);
            ui.TooltipIfHovered(r,
                "Партія сама записується в окреме гніздо «(авто)» через указаний\n"
                "проміжок ігрового часу. Ваші власні збереження це не чіпає.");
        }

        y += 8.0f;
        ui.Label(row(20.0f), "КЕРУВАННЯ", theme.accent);

        static const char* kBindings[] = {
            "WASD / стрілки — камера,  Q, E — обертати",
            "ЛКМ — вибрати, протягнути — рамка",
            "ПКМ — наказ,  Shift + ПКМ — пограбувати",
            "Пробіл, 0-5 — швидкість",
            "Shift + 1-4 — панелі,  F1 — кордони",
            "Esc — меню паузи",
        };
        for (const char* line : kBindings)
        {
            ui.Label(row(17.0f), line, theme.text);
        }
    }
}
