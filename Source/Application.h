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

        Window m_window;
        bool m_running = false;
    };
}
