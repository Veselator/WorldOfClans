#include "SettingsScene.h"
#include "SceneManager.h"

#include "../Core/Settings.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>

namespace woc
{
    void SettingsScene::OnEnter()
    {
        Renderer::Get().SetTerrainEnabled(false);
        Renderer::Get().SetClearColor(Theme::Get().background);

        Settings& settings = Settings::Get();
        for (size_t i = 0; i < settings.Resolutions().size(); ++i)
        {
            if (settings.Resolutions()[i].width == settings.windowWidth &&
                settings.Resolutions()[i].height == settings.windowHeight)
            {
                m_resolutionIndex = static_cast<i32>(i);
            }
        }
    }

    void SettingsScene::OnExit()
    {
        // Leaving the screen always persists: nobody expects to lose a toggle by pressing Esc.
        Settings::Get().Save();
    }

    void SettingsScene::Update(f32 deltaTime)
    {
        if (m_statusTimer > 0.0f) m_statusTimer -= deltaTime;
        if (Input::Get().WasKeyPressed(Key::Escape) && !UI::Get().WantsKeyboard())
        {
            SceneManager::Get().RequestBack();
        }
    }

    void SettingsScene::Render()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        const Vec2 viewport = renderer.ViewportSize();

        const Rect header{ 0.0f, 0.0f, viewport.x, 44.0f };
        renderer.UIRect(header, theme.panelHeader);
        renderer.UIText("НАЛАШТУВАННЯ", { 20.0f, 13.0f }, theme.accent, 1.3f);

        const f32 margin = 20.0f;
        const f32 top = header.Bottom() + margin;
        const f32 bottom = viewport.y - 70.0f;
        const f32 columnWidth = std::min(460.0f, (viewport.x - margin * 3.0f) / 2.0f);

        const f32 slideA = ui.SlideIn("settings.a", true, 50.0f, 0.22f);
        const f32 slideB = ui.SlideIn("settings.b", true, 50.0f, 0.30f);

        DrawDisplay({ margin - slideA, top, columnWidth, bottom - top });
        DrawGameplay({ margin * 2.0f + columnWidth - slideB, top, columnWidth, bottom - top });

        const Rect back{ margin, viewport.y - 54.0f, 200.0f, 34.0f };
        if (ui.Button(back, "Назад"))
        {
            SceneManager::Get().RequestBack();
        }

        if (m_statusTimer > 0.0f && !m_status.empty())
        {
            renderer.UIText(m_status, { margin + 220.0f, viewport.y - 46.0f }, theme.positive);
        }
    }

    void SettingsScene::DrawDisplay(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        Settings& settings = Settings::Get();

        ui.Panel(area, "Зображення");

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

        bool vsync = settings.vsync;
        if (ui.Toggle(row(26.0f), "Вертикальна синхронізація", vsync))
        {
            settings.vsync = vsync;
            m_status = "Синхронізація застосується після перезапуску";
            m_statusTimer = 5.0f;
        }

        bool borders = settings.showBorders;
        if (ui.Toggle(row(26.0f), "Показувати кордони (F1)", borders))
        {
            settings.showBorders = borders;
            Renderer::Get().SetBordersVisible(borders);
        }

        bool fps = settings.showFps;
        if (ui.Toggle(row(26.0f), "Показувати FPS", fps)) settings.showFps = fps;

        bool labels = settings.showLabels;
        if (ui.Toggle(row(26.0f), "Назви поселень", labels)) settings.showLabels = labels;

        ui.Slider(row(26.0f), "Масштаб для назв", settings.labelMinZoom, 0.5f, 3.5f);

        // The interface's own size. Applied on "Застосувати" rather than as the handle
        // moves: every panel on this very screen is laid out from these metrics, and
        // resizing them mid-frame would drag the slider out from under the cursor.
        y += 6.0f;
        ui.Slider(row(26.0f), "Масштаб інтерфейсу", settings.uiScale, 0.75f, 1.75f);
        {
            char scaleText[32];
            std::snprintf(scaleText, sizeof(scaleText), "%.0f%%", settings.uiScale * 100.0f);
            ui.LabelRight(row(18.0f), scaleText, theme.textDim);
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

    void SettingsScene::DrawGameplay(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        Settings& settings = Settings::Get();

        ui.Panel(area, "Камера та гра");

        const f32 innerX = area.x + theme.padding;
        const f32 innerW = area.w - theme.padding * 2.0f;
        f32 y = area.y + theme.headerHeight + theme.padding;

        auto row = [&](f32 height)
        {
            const Rect r{ innerX, y, innerW, height };
            y += height + 6.0f;
            return r;
        };

        // The camera's tilt is not here on purpose: the whole map is drawn around one
        // angle, and the only thing letting it be dragged about achieves is breaking it.

        ui.Slider(row(26.0f), "Швидкість обертання", settings.rotateSpeed, 20.0f, 220.0f);
        ui.Slider(row(26.0f), "Прокрутка від краю", settings.edgeScroll, 0.0f, 30.0f);

        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 10.0f;

        ui.Label(row(20.0f), "Швидкість гри за замовчуванням", theme.textDim);
        ui.Stepper(row(26.0f), "Крок", settings.defaultSpeedIndex, 0, 5);

        y += 10.0f;
        ui.Label(row(20.0f), "КЕРУВАННЯ", theme.accent);

        static const char* kBindings[] = {
            "WASD / стрілки — камера",
            "Q, E — обертати карту",
            "R — повернути кут",
            "Колесо — масштаб",
            "ЛКМ — вибрати, ПКМ — наказ",
            "Shift + ПКМ — пограбувати",
            "Пробіл, 0-5 — швидкість",
            "F1 — кордони",
            "F2/F3/F4 — держава, дипломатія, хроніка",
            "F5 — перечитати конфіги",
            "F9 — зберегти об'єкти карти",
            "Esc — меню паузи",
        };
        for (const char* line : kBindings)
        {
            ui.Label(row(18.0f), line, theme.text);
        }
    }
}
