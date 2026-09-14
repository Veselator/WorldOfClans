#include "Window.h"
#include "Input.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>

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

    namespace
    {
        // Ids of the copies baked in by Source/Resources.rc.
        constexpr WORD kIconResource = 101;
        constexpr WORD kCursorResource = 102;

        /// An .ico or .cur file from Sprites, or the copy in the executable when the file is
        /// not there. Loaded at its own size: it is pixel art and must not be resampled.
        HICON LoadPicture(HINSTANCE instance, const std::string& file, WORD resource, UINT type)
        {
            const std::wstring path = Widen(Paths::Get().Root() + "/Sprites/" + file);
            HANDLE image = LoadImageW(nullptr, path.c_str(), type, 0, 0, LR_LOADFROMFILE);
            if (!image) image = LoadImageW(instance, MAKEINTRESOURCEW(resource), IMAGE_ICON, 0, 0, 0);
            return static_cast<HICON>(image);
        }

        /// Reads a bitmap's pixels as 32-bit BGRA, top row first.
        bool ReadPixels(HBITMAP bitmap, i32 width, i32 height, std::vector<u32>& out)
        {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            out.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
            HDC dc = GetDC(nullptr);
            const int rows = GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), out.data(), &info, DIB_RGB_COLORS);
            ReleaseDC(nullptr, dc);
            return rows == height;
        }

        /// The game's pointer, `scale` times its drawn size. The picture is pixel art, so it
        /// is enlarged by repeating pixels rather than by smoothing them. A real cursor file
        /// carries its own hotspot; an icon-format file does not, so its tip is taken to be
        /// the top-left pixel.
        HCURSOR LoadGameCursor(HINSTANCE instance, f32 scale)
        {
            const std::string file = "Cursor.cur";
            const std::wstring path = Widen(Paths::Get().Root() + "/Sprites/" + file);

            // Bytes 2-3 of the header: 1 for an icon, 2 for a cursor.
            WORD kind = 0;
            if (FILE* handle = _wfopen(path.c_str(), L"rb"))
            {
                WORD header[2]{};
                if (std::fread(header, sizeof(WORD), 2, handle) == 2) kind = header[1];
                std::fclose(handle);
            }

            HICON source = nullptr;
            if (kind == 2) source = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE));
            if (!source) source = LoadPicture(instance, file, kCursorResource, IMAGE_ICON);
            if (!source) return LoadCursorW(nullptr, IDC_ARROW);

            ICONINFO info{};
            if (!GetIconInfo(source, &info))
            {
                DestroyIcon(source);
                return LoadCursorW(nullptr, IDC_ARROW);
            }
            const bool ownHotspot = kind == 2;
            HCURSOR cursor = nullptr;

            BITMAP shape{};
            if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(shape), &shape))
            {
                const i32 width = shape.bmWidth;
                const i32 height = shape.bmHeight;
                std::vector<u32> colour;
                std::vector<u32> mask;
                if (ReadPixels(info.hbmColor, width, height, colour))
                {
                    // An old picture without an alpha channel says what is see-through in its mask.
                    const bool hasAlpha = std::any_of(colour.begin(), colour.end(), [](u32 px) { return (px >> 24) != 0; });
                    if (!hasAlpha && info.hbmMask && ReadPixels(info.hbmMask, width, height, mask))
                    {
                        for (size_t i = 0; i < colour.size(); ++i)
                        {
                            if ((mask[i] & 0x00FFFFFF) == 0) colour[i] |= 0xFF000000u;
                        }
                    }

                    const f32 factor = std::max(0.25f, scale);
                    const i32 outWidth = std::max(1, static_cast<i32>(std::lround(static_cast<f32>(width) * factor)));
                    const i32 outHeight = std::max(1, static_cast<i32>(std::lround(static_cast<f32>(height) * factor)));

                    BITMAPV5HEADER header{};
                    header.bV5Size = sizeof(header);
                    header.bV5Width = outWidth;
                    header.bV5Height = -outHeight;
                    header.bV5Planes = 1;
                    header.bV5BitCount = 32;
                    header.bV5Compression = BI_BITFIELDS;
                    header.bV5RedMask = 0x00FF0000;
                    header.bV5GreenMask = 0x0000FF00;
                    header.bV5BlueMask = 0x000000FF;
                    header.bV5AlphaMask = 0xFF000000;

                    HDC dc = GetDC(nullptr);
                    void* bits = nullptr;
                    HBITMAP scaled = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
                    ReleaseDC(nullptr, dc);

                    // The monochrome mask, rows padded to 16 bits: set where the picture is clear.
                    const i32 stride = ((outWidth + 15) / 16) * 2;
                    std::vector<u8> maskBits(static_cast<size_t>(stride) * static_cast<size_t>(outHeight), 0);

                    if (scaled && bits)
                    {
                        u32* target = static_cast<u32*>(bits);
                        for (i32 y = 0; y < outHeight; ++y)
                        {
                            const i32 sy = std::min(height - 1, static_cast<i32>(static_cast<f32>(y) / factor));
                            for (i32 x = 0; x < outWidth; ++x)
                            {
                                const i32 sx = std::min(width - 1, static_cast<i32>(static_cast<f32>(x) / factor));
                                const u32 px = colour[static_cast<size_t>(sy) * static_cast<size_t>(width) + static_cast<size_t>(sx)];
                                target[static_cast<size_t>(y) * static_cast<size_t>(outWidth) + static_cast<size_t>(x)] = px;
                                if ((px >> 24) == 0)
                                {
                                    maskBits[static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x / 8)] |=
                                        static_cast<u8>(0x80 >> (x % 8));
                                }
                            }
                        }

                        HBITMAP scaledMask = CreateBitmap(outWidth, outHeight, 1, 1, maskBits.data());
                        ICONINFO made{};
                        made.fIcon = FALSE;
                        made.xHotspot = ownHotspot ? static_cast<DWORD>(std::lround(static_cast<f32>(info.xHotspot) * factor)) : 0;
                        made.yHotspot = ownHotspot ? static_cast<DWORD>(std::lround(static_cast<f32>(info.yHotspot) * factor)) : 0;
                        made.hbmColor = scaled;
                        made.hbmMask = scaledMask;
                        cursor = CreateIconIndirect(&made);
                        if (scaledMask) DeleteObject(scaledMask);
                    }
                    if (scaled) DeleteObject(scaled);
                }
            }

            if (info.hbmColor) DeleteObject(info.hbmColor);
            if (info.hbmMask) DeleteObject(info.hbmMask);
            DestroyIcon(source);
            return cursor ? cursor : LoadCursorW(nullptr, IDC_ARROW);
        }
    }

    bool Window::Create(const std::string& title, u32 width, u32 height)
    {
        m_instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&Window::WindowProc);
        wc.hInstance = m_instance;
        m_cursor = LoadGameCursor(m_instance, 1.0f);
        wc.hCursor = m_cursor;
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClassName;
        wc.hIcon = LoadPicture(m_instance, "Icon.ico", kIconResource, IMAGE_ICON);
        if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
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

    void Window::SetCursorScale(f32 scale)
    {
        if (!m_hwnd || std::abs(scale - m_cursorScale) < 0.001f) return;
        HCURSOR fresh = LoadGameCursor(m_instance, scale);
        if (!fresh) return;
        m_cursorScale = scale;

        // The class cursor is what Windows puts back whenever the pointer moves over the
        // window; SetCursor swaps the one showing right now.
        SetClassLongPtrW(m_hwnd, GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(fresh));
        SetCursor(fresh);
        if (m_cursor) DestroyCursor(m_cursor);
        m_cursor = fresh;
        WOC_LOG_INFO("Cursor scale ", scale);
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
