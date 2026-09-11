// UI.h - immediate-mode widget layer on top of the renderer.
//
// Widgets are drawn and queried in one call, which keeps screen code linear and makes
// the interface trivial to reorganise. Interaction state (which control is hovered or
// held) is the only thing that persists between frames.
#pragma once

#include "Theme.h"
#include "../Core/Singleton.h"
#include "../Render/RenderTypes.h"

#include <unordered_map>

namespace woc
{
    class UI final : public Singleton<UI>
    {
        friend class Singleton<UI>;
    public:
        void BeginFrame();
        void EndFrame();

        // --- animation ---------------------------------------------------------------------
        /// Eases a named 0..1 value towards `active`. Panels use it to slide and fade in
        /// instead of snapping, which is what makes the interface feel settled.
        f32 Transition(const std::string& id, bool active, f32 seconds = 0.18f);
        /// Convenience: horizontal slide-in offset for a panel of the given width.
        f32 SlideIn(const std::string& id, bool active, f32 distance, f32 seconds = 0.18f);
        /// Resets an animation so the next appearance plays from the start.
        void RestartTransition(const std::string& id);

        // --- containers -----------------------------------------------------------------------
        /// Draws a framed panel and marks the area as blocking world input.
        void Panel(const Rect& rect, const std::string& title = "");
        void PanelBody(const Rect& rect);
        void Separator(const Rect& rect);

        /// Scrollable region. Returns the rect content should be laid out in; the caller
        /// must call EndScroll when finished. `scroll` is owned by the caller.
        Rect BeginScroll(const Rect& rect, f32 contentHeight, f32& scroll);
        /// Pass the y at which the content actually ended. The region remembers it and uses
        /// it as the extent next frame, so a panel whose height is hard to predict up front
        /// still scrolls all the way down.
        void EndScroll(f32 contentBottom = 0.0f);

        // --- widgets ---------------------------------------------------------------------------
        /// Why a button cannot be pressed matters to the player. "You cannot do this here"
        /// and "you cannot afford this" are different answers, and a grey button says the
        /// first when the truth is often the second.
        enum class ButtonState
        {
            Ready,          // press it
            Unaffordable,   // allowed, but the treasury is short - drawn red
            Disabled        // not allowed at all - drawn grey
        };

        bool Button(const Rect& rect, const std::string& label, ButtonState state);
        /// Every widget that registers a press routes through here, so the click is heard
        /// once and in one place rather than at thirty call sites.
        static void PlayPressSound();
        bool Button(const Rect& rect, const std::string& label, bool enabled = true)
        {
            return Button(rect, label, enabled ? ButtonState::Ready : ButtonState::Disabled);
        }
        /// The common case: an action that is allowed but may be beyond the purse.
        bool Button(const Rect& rect, const std::string& label, bool allowed, bool affordable)
        {
            return Button(rect, label, !allowed ? ButtonState::Disabled
                                     : (affordable ? ButtonState::Ready : ButtonState::Unaffordable));
        }
        /// A clickable area with no chrome of its own, for custom-drawn controls.
        bool InvisibleButton(const Rect& rect, const std::string& id, bool enabled = true);
        /// True while the pointer is over `rect` and not hidden by a scroll clip.
        bool Hovered(const Rect& rect) const { return IsHovered(rect); }
        bool IconButton(const Rect& rect, SpriteId sprite, const std::string& tooltip, bool enabled = true);
        bool ListItem(const Rect& rect, const std::string& label, bool selected,
                      const Color& accentStripe = Color(0, 0, 0, 0));
        bool Toggle(const Rect& rect, const std::string& label, bool& value);
        bool Stepper(const Rect& rect, const std::string& label, i32& value, i32 minimum, i32 maximum);
        bool Slider(const Rect& rect, const std::string& label, f32& value, f32 minimum, f32 maximum);
        bool TextField(const Rect& rect, const std::string& id, std::string& value, size_t maxLength = 48);
        /// Length of a UTF-8 string in letters rather than in bytes.
        static size_t CountCharacters(const std::string& utf8);

        void Label(const Rect& rect, const std::string& text, const Color& color, f32 scale = 1.0f);
        void LabelCentered(const Rect& rect, const std::string& text, const Color& color, f32 scale = 1.0f);
        void LabelRight(const Rect& rect, const std::string& text, const Color& color, f32 scale = 1.0f);
        /// Key on the left, value right-aligned - the workhorse of every info panel.
        void KeyValue(const Rect& rect, const std::string& key, const std::string& value,
                      const Color& valueColor);
        void ProgressBar(const Rect& rect, f32 value01, const Color& fill, const std::string& overlay = "");
        void Chip(const Rect& rect, const std::string& text, const Color& color);
        /// Greedy word wrap at `maxWidth` pixels; used by tooltips and description blocks.
        static std::vector<std::string> WrapText(const std::string& text, f32 maxWidth, f32 scale = 1.0f);
        /// Draws wrapped text and returns the height it consumed.
        f32 Paragraph(const Rect& rect, const std::string& text, const Color& color, f32 scale = 1.0f);
        /// A clickable colour square; returns true when picked.
        bool ColorSwatch(const Rect& rect, const Color& color, bool selected, const std::string& id);

        /// Queues a tooltip for the end of the frame, positioned near the cursor.
        void Tooltip(const std::string& text);
        void TooltipIfHovered(const Rect& rect, const std::string& text);

        // --- modality ----------------------------------------------------------------------------
        /// While a modal is up, nothing outside it answers the mouse. The panels behind it
        /// are drawn earlier in the frame than the dialog itself, so the region has to be
        /// declared before any of them - the scene knows it has a dialog open long before
        /// it gets around to drawing one.
        void SetModalRegion(const Rect& rect) { m_modal = rect; m_hasModal = true; }
        void ClearModalRegion() { m_hasModal = false; }
        bool HasModal() const { return m_hasModal; }

        // --- input arbitration -------------------------------------------------------------------
        /// True when the pointer is over interface chrome; scenes use it to ignore world clicks.
        bool WantsMouse() const { return m_wantsMouse || m_wantsMouseNext; }
        bool WantsKeyboard() const { return !m_activeTextField.empty(); }
        void BlockMouse(const Rect& rect);

        f32 RowHeight() const { return Theme::Get().rowHeight; }

    private:
        UI() = default;
        ~UI() = default;

        static u32 HashId(const std::string& text, const Rect& rect);
        bool IsHovered(const Rect& rect) const;

        Vec2 m_mouse;
        std::unordered_map<std::string, f32> m_transitions;
        bool m_mouseDown = false;
        bool m_mousePressed = false;
        bool m_mouseReleased = false;
        bool m_wantsMouse = false;       // published: what the pointer was over last frame
        bool m_wantsMouseNext = false;   // accumulating for the frame being drawn

        u32 m_hotId = 0;
        u32 m_activeId = 0;
        std::string m_activeTextField;
        f32 m_caretTimer = 0.0f;

        Rect m_modal;
        bool m_hasModal = false;

        std::vector<Rect> m_clipRegions;

        // Measured extents of scroll regions, keyed by the caller's own scroll variable.
        std::vector<const f32*> m_scrollKeys;
        std::vector<f32> m_scrollTops;
        std::unordered_map<const f32*, f32> m_scrollExtents;   // stack of active scroll clips
        std::string m_tooltip;
    };
}
