#include "Window.h"
#include "Input.h"
#include "../Core/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

namespace woc
{
    bool Window::SetClipboardText(const std::string& text)
    {
        // The clipboard is UTF-16, so the string is widened first. A lobby code is five
        // ASCII characters, but nothing here assumes that.
        const int wide = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (wide <= 0) return false;

        HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(wide) * sizeof(wchar_t));
        if (!handle) return false;

        wchar_t* buffer = static_cast<wchar_t*>(GlobalLock(handle));
        if (!buffer)
        {
            GlobalFree(handle);
            return false;
        }
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer, wide);
        GlobalUnlock(handle);

        if (!OpenClipboard(nullptr))
        {
            GlobalFree(handle);
            return false;
        }
        EmptyClipboard();
        // The clipboard owns the block from here; freeing it would be a double free.
        const bool placed = SetClipboardData(CF_UNICODETEXT, handle) != nullptr;
        CloseClipboard();
        if (!placed) GlobalFree(handle);
        return placed;
    }

    namespace
    {
        constexpr const wchar_t* kClassName = L"WorldOfClansWindow";

        std::wstring Widen(const std::string& utf8)
        {
            if (utf8.empty()) return {};
            const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring result(static_cast<size_t>(needed), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), needed);
            return result;
        }
    }

    Window::~Window()
    {
        Destroy();
    }

    bool Window::Create(const std::string& title, u32 width, u32 height)
    {
        m_instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&Window::WindowProc);
        wc.hInstance = m_instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClassName;
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            WOC_LOG_ERROR("RegisterClassExW failed: ", GetLastError());
            return false;
        }

        RECT rect{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        const DWORD style = WS_OVERLAPPEDWINDOW;
        AdjustWindowRect(&rect, style, FALSE);

        const std::wstring wideTitle = Widen(title);
        m_hwnd = CreateWindowExW(
            0, kClassName, wideTitle.c_str(), style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, m_instance, this);

        if (!m_hwnd)
        {
            WOC_LOG_ERROR("CreateWindowExW failed: ", GetLastError());
            return false;
        }

        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);

        RECT client{};
        GetClientRect(m_hwnd, &client);
        m_width = static_cast<u32>(client.right - client.left);
        m_height = static_cast<u32>(client.bottom - client.top);
        WOC_LOG_INFO("Window created ", m_width, "x", m_height);
        return true;
    }

    void Window::Destroy()
    {
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    bool Window::PumpMessages()
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return !m_shouldClose;
    }

    bool Window::ConsumeResizeFlag()
    {
        const bool value = m_resized;
        m_resized = false;
        return value;
    }

    void Window::SetTitle(const std::string& title)
    {
        if (m_hwnd) SetWindowTextW(m_hwnd, Widen(title).c_str());
    }

    void Window::SetFullscreen(bool fullscreen)
    {
        if (!m_hwnd || fullscreen == m_fullscreen) return;

        if (fullscreen)
        {
            RECT rect{};
            GetWindowRect(m_hwnd, &rect);
            m_windowedRect[0] = rect.left;
            m_windowedRect[1] = rect.top;
            m_windowedRect[2] = rect.right - rect.left;
            m_windowedRect[3] = rect.bottom - rect.top;

            // Borderless rather than exclusive: alt-tab stays instant and the Vulkan
            // swapchain does not have to negotiate a display mode.
            HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{ sizeof(MONITORINFO) };
            GetMonitorInfoW(monitor, &info);

            SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(m_hwnd, HWND_TOP,
                         info.rcMonitor.left, info.rcMonitor.top,
                         info.rcMonitor.right - info.rcMonitor.left,
                         info.rcMonitor.bottom - info.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        else
        {
            SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowPos(m_hwnd, HWND_NOTOPMOST,
                         m_windowedRect[0], m_windowedRect[1],
                         m_windowedRect[2], m_windowedRect[3],
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }

        m_fullscreen = fullscreen;
        m_resized = true;
    }

    void Window::SetClientSize(u32 width, u32 height)
    {
        if (!m_hwnd || m_fullscreen || width == 0 || height == 0) return;

        RECT rect{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(m_hwnd, nullptr, 0, 0,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
        m_resized = true;
    }

    i64 __stdcall Window::WindowProc(HWND__* hwnd, u32 msg, u64 wparam, i64 lparam)
    {
        Window* self = nullptr;
        if (msg == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<Window*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->m_hwnd = hwnd;
        }
        else
        {
            self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) return self->HandleMessage(msg, wparam, lparam);
        return DefWindowProcW(hwnd, msg, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
    }

    i64 Window::HandleMessage(u32 msg, u64 wparam, i64 lparam)
    {
        Input& input = Input::Get();

        switch (msg)
        {
        case WM_CLOSE:
            WOC_LOG_INFO("Window received WM_CLOSE");
            m_shouldClose = true;
            return 0;

        case WM_DESTROY:
            WOC_LOG_INFO("Window received WM_DESTROY");
            m_shouldClose = true;
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
        {
            const u32 width = static_cast<u32>(LOWORD(lparam));
            const u32 height = static_cast<u32>(HIWORD(lparam));
            if (width != m_width || height != m_height)
            {
                m_width = width;
                m_height = height;
                m_resized = true;
            }
            return 0;
        }

        case WM_KILLFOCUS:
            input.OnFocusLost();
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            input.OnKey(Input::FromVirtualKey(static_cast<u32>(wparam)), true);
            if (wparam == VK_F4 && (GetKeyState(VK_MENU) & 0x8000))
            {
                WOC_LOG_INFO("Alt+F4 requested");
                m_shouldClose = true;
            }
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            input.OnKey(Input::FromVirtualKey(static_cast<u32>(wparam)), false);
            return 0;

        case WM_CHAR:
        {
            const u32 code = static_cast<u32>(wparam);
            // Reassemble surrogate pairs so non-BMP characters survive.
            if (code >= 0xD800 && code <= 0xDBFF) { m_highSurrogate = code; return 0; }
            if (code >= 0xDC00 && code <= 0xDFFF)
            {
                if (m_highSurrogate)
                {
                    const u32 combined = 0x10000 + ((m_highSurrogate - 0xD800) << 10) + (code - 0xDC00);
                    input.OnTextCharacter(combined);
                    m_highSurrogate = 0;
                }
                return 0;
            }
            input.OnTextCharacter(code);
            return 0;
        }

        case WM_LBUTTONDOWN: SetCapture(m_hwnd); input.OnMouseButton(MouseButton::Left, true);   return 0;
        case WM_LBUTTONUP:   ReleaseCapture();   input.OnMouseButton(MouseButton::Left, false);  return 0;
        case WM_RBUTTONDOWN: input.OnMouseButton(MouseButton::Right, true);   return 0;
        case WM_RBUTTONUP:   input.OnMouseButton(MouseButton::Right, false);  return 0;
        case WM_MBUTTONDOWN: input.OnMouseButton(MouseButton::Middle, true);  return 0;
        case WM_MBUTTONUP:   input.OnMouseButton(MouseButton::Middle, false); return 0;

        case WM_MOUSEMOVE:
            input.OnMouseMove(static_cast<f32>(GET_X_LPARAM(lparam)), static_cast<f32>(GET_Y_LPARAM(lparam)));
            return 0;

        case WM_MOUSEWHEEL:
            input.OnMouseWheel(static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<f32>(WHEEL_DELTA));
            return 0;

        case WM_ERASEBKGND:
            return 1; // Vulkan owns the client area; skip GDI clearing to avoid flicker.

        default:
            break;
        }

        return DefWindowProcW(m_hwnd, msg, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
    }
}
