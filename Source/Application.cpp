#include "Application.h"

#include "Core/Config.h"
#include "Core/EventBus.h"
#include "Core/JobSystem.h"
#include "Core/Profiler.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Audio/AudioSystem.h"
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

#include <cstdio>

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

        // The workers are started before anything can ask for one, and torn down last.
        JobSystem::Get().Start();
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

        // Sound is a comfort, not a requirement: a machine without a device still plays.
        if (AudioSystem::Get().Initialise())
        {
            AudioSystem::Get().SetMusicVolume(Settings::Get().musicVolume);
            AudioSystem::Get().SetSfxVolume(Settings::Get().sfxVolume);
        }

        SceneManager& scenes = SceneManager::Get();
        Input& input = Input::Get();

        m_running = true;
        while (m_running)
        {
            Profiler::Get().Push("os.pump");
            input.BeginFrame();
            const bool pumped = m_window.PumpMessages();
            Profiler::Get().Pop();
            if (!pumped) break;

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

            // Profiled from the previous frame's scope set: the wait for the GPU happens
            // here, and leaving it unmeasured is what made every frame look like it cost
            // nothing at all.
            Profiler::Get().Push("frame.wait");
            const bool ready = renderer.BeginFrame();
            Profiler::Get().Pop();
            if (!ready)
            {
                continue;
            }

            const f32 deltaTime = renderer.DeltaTime();

            // F5 reloads the JSON balance files without restarting the game.
#ifdef _DEBUG
            // F11 puts the frame breakdown on screen; F12 dumps the worst frame so far.
            // Neither exists in a release build, and nor does the profiler behind them.
            if (input.WasKeyPressed(Key::F11))
            {
                m_showProfiler = !m_showProfiler;
            }
            if (input.WasKeyPressed(Key::F12))
            {
                Profiler& profiler = Profiler::Get();
                std::string dump;
                for (const Profiler::Entry& entry : profiler.WorstBreakdown())
                {
                    if (entry.milliseconds < 0.05) continue;
                    dump += "\n    ";
                    dump.append(static_cast<size_t>(entry.depth) * 2, ' ');
                    dump += entry.name + ": " + std::to_string(entry.milliseconds) + " ms";
                }
                WOC_LOG_INFO("Worst frame in window: ", profiler.WorstFrameMilliseconds(),
                             " ms, spikes so far: ", profiler.SpikeCount(), dump);
            }
#endif

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

            Profiler& profiler = Profiler::Get();

            {
                WOC_PROFILE("audio");
                AudioSystem::Get().Update(deltaTime);
            }

            UI::Get().BeginFrame();
            {
                WOC_PROFILE("scene.update");
                scenes.Update(deltaTime);
            }
            {
                WOC_PROFILE("scene.render");
                scenes.Render();
            }
            DrawFrameRate(deltaTime);
            DrawProfiler();
            UI::Get().EndFrame();

            {
                // Everything the GPU is told to do, plus the wait for the frame two back.
                WOC_PROFILE("gpu.submit");
                renderer.EndFrame();
            }

            profiler.EndFrame(static_cast<f64>(deltaTime) * 1000.0);
            profiler.BeginFrame();
        }

        Shutdown();
        return 0;
    }

    void Application::DrawProfiler()
    {
#ifdef _DEBUG
        if (!m_showProfiler) return;

        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        Profiler& profiler = Profiler::Get();

        const f32 lineHeight = renderer.TextHeight(0.9f) + 2.0f;
        const std::vector<Profiler::Entry>& entries = profiler.Entries();

        const Rect plate{ 4.0f, 30.0f, 330.0f, lineHeight * (entries.size() + 3) + 12.0f };
        renderer.UIRect(plate, theme.shadow.WithAlpha(0.82f));

        f32 y = plate.y + 6.0f;
        char buffer[160];

        std::snprintf(buffer, sizeof(buffer), "frame %.2f ms   worst %.2f   spikes %u",
                      profiler.LastFrameMilliseconds(), profiler.WorstFrameMilliseconds(),
                      profiler.SpikeCount());
        renderer.UIText(buffer, { plate.x + 6.0f, y }, theme.accent, 0.9f);
        y += lineHeight * 1.5f;

        for (const Profiler::Entry& entry : entries)
        {
            // Indented by nesting, so a costly child is visibly inside its parent.
            std::string label(static_cast<size_t>(entry.depth) * 2, ' ');
            label += entry.name;

            std::snprintf(buffer, sizeof(buffer), "%.2f / %.2f", entry.milliseconds, entry.peak);

            // Red for what is costing a frame right now, amber for what has spiked in the
            // window, grey for the rest.
            const Color color = entry.milliseconds > 4.0 ? theme.negative
                              : entry.peak > 4.0        ? theme.warning
                                                        : theme.textDim;
            renderer.UIText(label, { plate.x + 6.0f, y }, color, 0.9f);
            renderer.UIText(buffer, { plate.Right() - renderer.TextWidth(buffer, 0.9f) - 6.0f, y },
                            color, 0.9f);
            y += lineHeight;
        }
#endif
    }

    void Application::DrawFrameRate(f32 deltaTime)
    {
        if (!Settings::Get().showFps) return;

        // Averaged over a quarter second: a per-frame number is unreadable and a longer
        // window would hide exactly the stutters this counter exists to reveal.
        m_fpsAccumulator += deltaTime;
        ++m_fpsFrames;
        if (m_fpsAccumulator >= 0.25f)
        {
            m_fpsSmoothed = static_cast<f32>(m_fpsFrames) / m_fpsAccumulator;
            m_fpsAccumulator = 0.0f;
            m_fpsFrames = 0;
        }

        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f FPS", m_fpsSmoothed);

        const f32 width = renderer.TextWidth(buffer) + 10.0f;
        const Rect plate{ 4.0f, 4.0f, width, renderer.TextHeight() + 6.0f };
        renderer.UIRect(plate, theme.shadow.WithAlpha(0.65f));

        const Color color = m_fpsSmoothed < 30.0f ? theme.negative
                          : m_fpsSmoothed < 55.0f ? theme.warning
                                                  : theme.positive;
        renderer.UIText(buffer, { plate.x + 5.0f, plate.y + 3.0f }, color);
    }

    void Application::Shutdown()
    {
        WOC_LOG_INFO("Shutting down");
        SceneManager::Get().Shutdown();
        EventBus::Get().Clear();
        AudioSystem::Get().Shutdown();
        Renderer::Get().Shutdown();
        m_window.Destroy();
        // Last: a worker may still be finishing a job whose result nobody will collect.
        JobSystem::Get().Shutdown();
    }
}
