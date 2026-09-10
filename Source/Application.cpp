#include "Application.h"

#include "Core/Config.h"
#include "Core/EventBus.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Core/Settings.h"
#include "Game/Factories/NamePool.h"
#include "Game/Map/TerrainTypes.h"
#include "Game/World/BuildingDatabase.h"
#include "Game/World/RaceDatabase.h"
#include "Game/World/SettlementDatabase.h"
#include "Game/World/UnitDatabase.h"
#include "Platform/Input.h"
#include "Render/Renderer.h"
#include "Scenes/SceneFactory.h"
#include "Scenes/SceneManager.h"
#include "UI/Theme.h"
#include "UI/UI.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace woc
{
    void Application::BootServices()
    {
        Paths::Get().Initialise();
        Logger::Get().Open(Paths::Get().Root() + "/WorldOfClans.log");
        WOC_LOG_INFO("World of Clans starting up");

        ConfigManager::Get().LoadAll();
        LoadDatabases();
        Theme::Get().Load();
    }

    void Application::LoadDatabases()
    {
        // Every database is a pure projection of a JSON document, so reloading them is all
        // that a configuration hot-reload needs to do.
        TerrainDatabase::Get().Load();
        RaceDatabase::Get().Load();
        UnitDatabase::Get().Load();
        SettlementDatabase::Get().Load();
        BuildingDatabase::Get().Load();
        NamePool::Get().Load();
    }

    void Application::RegisterScenes()
    {
        SceneFactory::RegisterAll(SceneManager::Get());
        SceneManager::Get().Request(SceneId::MainMenu);
    }

    int Application::Run()
    {
        BootServices();

        ConfigManager& config = ConfigManager::Get();
        Settings& settings = Settings::Get();
        settings.Load();

        const std::string title = config.Str("window/title", "World of Clans");
        if (!m_window.Create(title, settings.windowWidth, settings.windowHeight))
        {
            WOC_LOG_ERROR("Could not create the application window");
            return 1;
        }

        Renderer& renderer = Renderer::Get();
        try
        {
            renderer.Initialise(m_window);
        }
        catch (const std::exception& error)
        {
            WOC_LOG_ERROR("Renderer initialisation failed: ", error.what());
#ifdef _WIN32
            MessageBoxA(nullptr, error.what(), "World of Clans - Vulkan error", MB_OK | MB_ICONERROR);
#endif
            return 2;
        }

        // The window exists and Vulkan is up: now the player's own preferences take effect.
        settings.Apply(m_window);
        if (m_window.ConsumeResizeFlag()) renderer.OnResize(m_window.Width(), m_window.Height());

        RegisterScenes();

        SceneManager& scenes = SceneManager::Get();
        Input& input = Input::Get();

        m_running = true;
        while (m_running)
        {
            input.BeginFrame();
            if (!m_window.PumpMessages()) break;

            if (m_window.ConsumeResizeFlag())
            {
                renderer.OnResize(m_window.Width(), m_window.Height());
            }

            scenes.ApplyPendingTransition();
            if (scenes.QuitRequested() || !scenes.Current())
            {
                WOC_LOG_INFO("Loop ending: quitRequested=", scenes.QuitRequested(),
                             " hasScene=", scenes.Current() != nullptr);
                break;
            }

            if (!renderer.BeginFrame())
            {
                continue;
            }

            const f32 deltaTime = renderer.DeltaTime();

            // F5 reloads the JSON balance files without restarting the game.
            if (input.WasKeyPressed(Key::F5))
            {
                config.Reload();
                LoadDatabases();
                Theme::Get().Load();
                WOC_LOG_INFO("Configuration hot-reloaded");
            }

            if (scenes.Payload("applySettings") == "1")
            {
                scenes.SetPayload("applySettings", "");
                Settings::Get().Apply(m_window);
            }

            UI::Get().BeginFrame();
            scenes.Update(deltaTime);
            scenes.Render();
            UI::Get().EndFrame();

            renderer.EndFrame();
        }

        Shutdown();
        return 0;
    }

    void Application::Shutdown()
    {
        WOC_LOG_INFO("Shutting down");
        SceneManager::Get().Shutdown();
        EventBus::Get().Clear();
        Renderer::Get().Shutdown();
        m_window.Destroy();
    }
}
