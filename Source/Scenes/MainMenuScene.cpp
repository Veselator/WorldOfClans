#include "MainMenuScene.h"
#include "SceneManager.h"
#include "../Core/Config.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>

namespace woc
{
    void MainMenuScene::OnEnter()
    {
        Renderer::Get().SetTerrainEnabled(false);
        Renderer::Get().SetClearColor(Theme::Get().background);
        m_saves = SaveGame::List();
        m_loadOpen = false;
        m_selectedSave = m_saves.empty() ? -1 : 0;
    }

    void MainMenuScene::Update(f32 deltaTime)
    {
        m_time += deltaTime;
    }

    void MainMenuScene::Render()
    {
        DrawBackdrop();
        DrawMenu();
        DrawLoadDialog();
    }

    void MainMenuScene::DrawBackdrop()
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        const Vec2 viewport = renderer.ViewportSize();

        // A slow drift of banner folk keeps the title screen from feeling dead.
        for (int i = 0; i < 24; ++i)
        {
            const f32 phase = m_time * 12.0f + static_cast<f32>(i) * 137.0f;
            const f32 x = std::fmod(phase, viewport.x + 80.0f) - 40.0f;
            const f32 y = viewport.y * 0.5f + std::sin(m_time * 0.6f + i) * 140.0f + (i % 5) * 26.0f;
            renderer.UISprite(static_cast<SpriteId>(i % 3), { x, y, 22.0f, 22.0f },
                              theme.border.WithAlpha(0.28f));
        }
    }

    void MainMenuScene::DrawMenu()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();
        const Json& menu = ConfigManager::Get().UI()["menu"];
        const Vec2 viewport = renderer.ViewportSize();

        const f32 panelWidth = 460.0f;
        const f32 panelHeight = 424.0f;
        const f32 rise = ui.SlideIn("menu.panel", true, 40.0f, 0.35f);

        const Rect panel{ (viewport.x - panelWidth) * 0.5f,
                          (viewport.y - panelHeight) * 0.5f + rise, panelWidth, panelHeight };
        ui.Panel(panel);

        const Rect title{ panel.x, panel.y + 26.0f, panel.w, 40.0f };
        renderer.UITextCentered(menu["title"].AsString("WORLD OF CLANS"), title, theme.accent, 2.0f);

        const Rect subtitle{ panel.x, title.Bottom() + 6.0f, panel.w, 20.0f };
        renderer.UITextCentered(menu["subtitle"].AsString(), subtitle, theme.textDim);

        renderer.UIRect({ panel.x + 60.0f, subtitle.Bottom() + 16.0f, panel.w - 120.0f, 1.0f }, theme.border);

        const f32 buttonWidth = 260.0f;
        const f32 x = panel.Center().x - buttonWidth * 0.5f;
        f32 y = subtitle.Bottom() + 40.0f;

        auto item = [&](size_t index, const char* fallback)
        {
            const std::string label = menu["items"][index].AsString(fallback);
            const Rect rect{ x, y, buttonWidth, 34.0f };
            y += 44.0f;
            return ui.Button(rect, label, !m_loadOpen);
        };

        if (item(0, "Нова партія")) SceneManager::Get().Request(SceneId::PartySetup);

        if (item(1, "Завантажити гру"))
        {
            m_saves = SaveGame::List();
            m_selectedSave = m_saves.empty() ? -1 : 0;
            m_loadOpen = true;
            ui.RestartTransition("menu.load");
        }

        if (item(2, "Редактор карт")) SceneManager::Get().Request(SceneId::Editor);
        if (item(3, "Налаштування")) SceneManager::Get().Request(SceneId::Settings);
        if (item(4, "Вихід")) SceneManager::Get().RequestQuit();

        const Rect footer{ panel.x, panel.Bottom() - 26.0f, panel.w, 20.0f };
        renderer.UITextCentered("Vulkan  |  " + m_version, footer, theme.textDim);
    }

    void MainMenuScene::DrawLoadDialog()
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        const f32 fade = ui.Transition("menu.load", m_loadOpen, 0.16f);
        if (fade <= 0.001f) return;

        const Vec2 viewport = renderer.ViewportSize();
        renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, theme.shadow.WithAlpha(0.65f * fade));

        const f32 width = 520.0f;
        const f32 height = 420.0f;
        const Rect panel{ (viewport.x - width) * 0.5f,
                          (viewport.y - height) * 0.5f + (1.0f - fade) * 30.0f, width, height };
        ui.Panel(panel, "Завантажити гру");
        ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const Rect body = Rect{ panel.x, panel.y + theme.headerHeight,
                                panel.w, panel.h - theme.headerHeight - 50.0f }.Inset(theme.padding);

        if (m_saves.empty())
        {
            ui.LabelCentered({ body.x, body.y + 40.0f, body.w, 24.0f }, "Збережень немає", theme.textDim);
        }
        else
        {
            const f32 rowHeight = 46.0f;
            const Rect content = ui.BeginScroll(body, m_saves.size() * rowHeight, m_saveScroll);
            for (size_t i = 0; i < m_saves.size(); ++i)
            {
                const Rect row{ content.x, content.y + i * rowHeight, content.w, rowHeight - 4.0f };
                if (ui.ListItem(row, m_saves[i].name, static_cast<i32>(i) == m_selectedSave))
                {
                    m_selectedSave = static_cast<i32>(i);
                }
                ui.LabelRight({ row.x, row.y, row.w - theme.padding, 20.0f },
                              m_saves[i].dateText, theme.textDim);
                ui.LabelRight({ row.x, row.y + 20.0f, row.w - theme.padding, 20.0f },
                              m_saves[i].realmName, theme.textDim);
            }
            ui.EndScroll();
        }

        const f32 buttonY = panel.Bottom() - 42.0f;
        const f32 buttonWidth = (panel.w - theme.padding * 4.0f) / 3.0f;

        if (ui.Button({ panel.x + theme.padding, buttonY, buttonWidth, 32.0f }, "Назад"))
        {
            m_loadOpen = false;
        }

        const bool hasSelection = m_selectedSave >= 0 && m_selectedSave < static_cast<i32>(m_saves.size());

        if (ui.Button({ panel.x + theme.padding * 2.0f + buttonWidth, buttonY, buttonWidth, 32.0f },
                      "Видалити", hasSelection))
        {
            SaveGame::Delete(m_saves[static_cast<size_t>(m_selectedSave)].fileName);
            m_saves = SaveGame::List();
            m_selectedSave = m_saves.empty() ? -1 : 0;
        }

        if (ui.Button({ panel.x + theme.padding * 3.0f + buttonWidth * 2.0f, buttonY, buttonWidth, 32.0f },
                      "Завантажити", hasSelection))
        {
            const SaveSlot& slot = m_saves[static_cast<size_t>(m_selectedSave)];
            SceneManager& scenes = SceneManager::Get();
            scenes.SetPayload("map", slot.mapFolder);
            scenes.SetPayload("load", slot.fileName);
            scenes.Request(SceneId::Game);
            m_loadOpen = false;
        }
    }
}
