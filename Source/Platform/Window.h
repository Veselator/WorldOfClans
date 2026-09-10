// Window.h - thin Win32 window wrapper feeding the Input singleton.
#pragma once

#include "../Core/Types.h"
#include "../Core/Math.h"
#include <functional>

struct HWND__;
struct HINSTANCE__;

namespace woc
{
    class Window
    {
    public:
        Window() = default;
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        bool Create(const std::string& title, u32 width, u32 height);
        void Destroy();

        /// Drains the OS message queue. Returns false once the window has been closed.
        bool PumpMessages();

        void RequestClose() { m_shouldClose = true; }
        bool ShouldClose() const { return m_shouldClose; }

        u32 Width() const { return m_width; }
        u32 Height() const { return m_height; }
        bool IsMinimised() const { return m_width == 0 || m_height == 0; }

        /// True exactly once after the client area changed, so the swapchain can rebuild.
        bool ConsumeResizeFlag();

        void SetTitle(const std::string& title);

        /// Borderless full screen on the monitor the window is currently on.
        void SetFullscreen(bool fullscreen);
        bool IsFullscreen() const { return m_fullscreen; }
        /// Resizes the client area; ignored while full screen.
        void SetClientSize(u32 width, u32 height);

        HWND__* NativeHandle() const { return m_hwnd; }
        HINSTANCE__* NativeInstance() const { return m_instance; }

    private:
        static i64 __stdcall WindowProc(HWND__* hwnd, u32 msg, u64 wparam, i64 lparam);
        i64 HandleMessage(u32 msg, u64 wparam, i64 lparam);

        HWND__* m_hwnd = nullptr;
        HINSTANCE__* m_instance = nullptr;
        u32 m_width = 0;
        u32 m_height = 0;
        bool m_shouldClose = false;
        bool m_resized = false;
        bool m_fullscreen = false;
        u32 m_highSurrogate = 0;
        i32 m_windowedRect[4]{};   // left, top, width, height before going full screen
    };
}
