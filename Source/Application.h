// Application.h - composition root: owns the window, boots every service, runs the loop.
#pragma once

#include "Core/Types.h"
#include "Platform/Window.h"

namespace woc
{
    class Application
    {
    public:
        int Run();

    private:
        void BootServices();
        void LoadDatabases();
        void RegisterScenes();
        void Shutdown();
        /// Draws the frame counter over whatever scene is running.
        void DrawFrameRate(f32 deltaTime);
        /// The frame breakdown, on F11. Left in the Release build on purpose: a profiler
        /// you have to rebuild the game to use is a profiler nobody uses.
        void DrawProfiler();

        f32 m_fpsAccumulator = 0.0f;
        f32 m_fpsSmoothed = 0.0f;
        bool m_showProfiler = false;
        i32 m_fpsFrames = 0;

        Window m_window;
        bool m_running = false;
    };
}
