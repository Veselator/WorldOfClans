#include "Input.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace woc
{
    void Input::BeginFrame()
    {
        m_previousKeys = m_keys;
        m_previousMouse = m_mouse;
        m_previousMousePosition = m_mousePosition;
        m_wheel = 0.0f;
        m_typedText.clear();
    }

    void Input::OnKey(Key key, bool down)
    {
        if (key == Key::Unknown) return;
        m_keys[Index(key)] = down;
    }

    void Input::OnMouseButton(MouseButton button, bool down)
    {
        m_mouse[static_cast<size_t>(button)] = down;
    }

    void Input::OnMouseMove(f32 x, f32 y)
    {
        m_mousePosition = { x, y };
    }

    void Input::OnMouseWheel(f32 delta)
    {
        m_wheel += delta;
    }

    void Input::OnTextCharacter(u32 codepoint)
    {
        // Control characters are handled through the key path, not the text path.
        if (codepoint < 0x20 || codepoint == 0x7F) return;
        if (codepoint < 0x80)
        {
            m_typedText += static_cast<char>(codepoint);
        }
        else if (codepoint < 0x800)
        {
            m_typedText += static_cast<char>(0xC0 | (codepoint >> 6));
            m_typedText += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            m_typedText += static_cast<char>(0xE0 | (codepoint >> 12));
            m_typedText += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            m_typedText += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    void Input::OnFocusLost()
    {
        // Without this, a key held while alt-tabbing would appear stuck down forever.
        m_keys.fill(false);
        m_mouse.fill(false);
    }

    Key Input::FromVirtualKey(u32 vk)
    {
#ifdef _WIN32
        switch (vk)
        {
        case VK_ESCAPE:    return Key::Escape;
        case VK_RETURN:    return Key::Enter;
        case VK_SPACE:     return Key::Space;
        case VK_TAB:       return Key::Tab;
        case VK_BACK:      return Key::Backspace;
        case VK_DELETE:    return Key::Delete;
        case VK_LEFT:      return Key::Left;
        case VK_RIGHT:     return Key::Right;
        case VK_UP:        return Key::Up;
        case VK_DOWN:      return Key::Down;
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:       return Key::Shift;
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return Key::Control;
        case VK_MENU: case VK_LMENU: case VK_RMENU:          return Key::Alt;
        case VK_OEM_PLUS: case VK_ADD:      return Key::Plus;
        case VK_OEM_MINUS: case VK_SUBTRACT: return Key::Minus;
        default: break;
        }
        if (vk >= VK_F1 && vk <= VK_F12)
            return static_cast<Key>(static_cast<u16>(Key::F1) + (vk - VK_F1));
        if (vk >= '0' && vk <= '9')
            return static_cast<Key>(static_cast<u16>(Key::Num0) + (vk - '0'));
        if (vk >= 'A' && vk <= 'Z')
            return static_cast<Key>(static_cast<u16>(Key::A) + (vk - 'A'));
#else
        (void)vk;
#endif
        return Key::Unknown;
    }
}
