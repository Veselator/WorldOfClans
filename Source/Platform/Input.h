// Input.h - frame-coherent keyboard and mouse state.
//
// The window pump feeds raw events in; every consumer (camera, UI, scenes) reads the
// same snapshot for the duration of a frame, so "was pressed this frame" queries are
// unambiguous no matter the order in which systems update.
#pragma once

#include "../Core/Singleton.h"
#include "../Core/Math.h"
#include <array>
#include <string>

namespace woc
{
    enum class MouseButton { Left = 0, Right = 1, Middle = 2, Count = 3 };

    enum class Key : u16
    {
        Unknown = 0,
        Escape, Enter, Space, Tab, Backspace, Delete,
        Left, Right, Up, Down,
        Shift, Control, Alt,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Plus, Minus,
        Count
    };

    class Input final : public Singleton<Input>
    {
        friend class Singleton<Input>;
    public:
        /// Rolls "current" into "previous" and clears per-frame accumulators.
        void BeginFrame();

        // --- feed from the platform layer ------------------------------------------------
        void OnKey(Key key, bool down);
        void OnMouseButton(MouseButton button, bool down);
        void OnMouseMove(f32 x, f32 y);
        void OnMouseWheel(f32 delta);
        void OnTextCharacter(u32 codepoint);
        void OnFocusLost();

        // --- queries ---------------------------------------------------------------------
        bool IsKeyDown(Key key) const { return m_keys[Index(key)]; }
        bool WasKeyPressed(Key key) const { return m_keys[Index(key)] && !m_previousKeys[Index(key)]; }
        bool WasKeyReleased(Key key) const { return !m_keys[Index(key)] && m_previousKeys[Index(key)]; }

        bool IsMouseDown(MouseButton b) const { return m_mouse[static_cast<size_t>(b)]; }
        bool WasMousePressed(MouseButton b) const
        {
            return m_mouse[static_cast<size_t>(b)] && !m_previousMouse[static_cast<size_t>(b)];
        }
        bool WasMouseReleased(MouseButton b) const
        {
            return !m_mouse[static_cast<size_t>(b)] && m_previousMouse[static_cast<size_t>(b)];
        }

        Vec2 MousePosition() const { return m_mousePosition; }
        Vec2 MouseDelta() const { return m_mousePosition - m_previousMousePosition; }
        f32 WheelDelta() const { return m_wheel; }
        const std::string& TypedText() const { return m_typedText; }

        /// Translates a Win32 virtual key code into the engine's Key enum.
        static Key FromVirtualKey(u32 vk);

    private:
        Input() = default;
        ~Input() = default;

        static size_t Index(Key key) { return static_cast<size_t>(key); }

        std::array<bool, static_cast<size_t>(Key::Count)> m_keys{};
        std::array<bool, static_cast<size_t>(Key::Count)> m_previousKeys{};
        std::array<bool, static_cast<size_t>(MouseButton::Count)> m_mouse{};
        std::array<bool, static_cast<size_t>(MouseButton::Count)> m_previousMouse{};

        Vec2 m_mousePosition;
        Vec2 m_previousMousePosition;
        f32 m_wheel = 0.0f;
        std::string m_typedText;
    };
}
