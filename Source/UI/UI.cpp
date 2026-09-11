#include "UI.h"
#include "../Audio/AudioSystem.h"
#include "../Platform/Input.h"
#include "../Render/Renderer.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        Color Mix(const Color& base, const Color& highlight, f32 amount)
        {
            return LerpColor(base, highlight, amount);
        }
    }

    u32 UI::HashId(const std::string& text, const Rect& rect)
    {
        u32 hash = 2166136261u;
        for (char c : text) { hash ^= static_cast<u8>(c); hash *= 16777619u; }
        hash ^= static_cast<u32>(rect.x) * 73856093u;
        hash ^= static_cast<u32>(rect.y) * 19349663u;
        return hash == 0 ? 1u : hash;
    }

    void UI::BeginFrame()
    {
        Input& input = Input::Get();
        m_mouse = input.MousePosition();
        m_mouseDown = input.IsMouseDown(MouseButton::Left);
        m_mousePressed = input.WasMousePressed(MouseButton::Left);
        m_mouseReleased = input.WasMouseReleased(MouseButton::Left);
        // Scenes read WantsMouse during Update, before any panel has been drawn this frame,
        // so the published flag lags one frame and the union of both is what callers see.
        m_wantsMouse = m_wantsMouseNext;
        m_wantsMouseNext = false;
        m_hotId = 0;
        m_hasModal = false;
        m_tooltip.clear();
        m_clipRegions.clear();
        m_caretTimer += Renderer::Get().DeltaTime();

        // Clicking outside every text field drops keyboard focus.
        if (m_mousePressed) m_activeTextField.clear();
    }

    void UI::EndFrame()
    {
        // The active widget is released only after every control has had its chance to
        // see the mouse-up; clearing it earlier would swallow the click that caused it.
        if (!m_mouseDown) m_activeId = 0;

        if (m_tooltip.empty()) return;

        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        const f32 lineHeight = renderer.TextHeight();
        const std::vector<std::string> lines = WrapText(m_tooltip, theme.tooltipMaxWidth);
        f32 widest = 0.0f;
        for (const std::string& line : lines) widest = std::max(widest, renderer.TextWidth(line));

        const Vec2 viewport = renderer.ViewportSize();
        const f32 width = widest + theme.padding * 2.0f;
        const f32 height = lines.size() * lineHeight + theme.padding * 2.0f;
        f32 x = m_mouse.x + 16.0f;
        f32 y = m_mouse.y + 18.0f;
        if (x + width > viewport.x) x = m_mouse.x - width - 8.0f;
        if (y + height > viewport.y) y = viewport.y - height - 4.0f;

        const Rect box{ x, y, width, height };
        renderer.UIRect({ box.x + 2.0f, box.y + 2.0f, box.w, box.h }, theme.shadow.WithAlpha(0.55f));
        renderer.UIRect(box, theme.panelAlt.WithAlpha(0.97f));
        renderer.UIRectOutline(box, theme.borderStrong, theme.borderThickness);
        for (size_t i = 0; i < lines.size(); ++i)
        {
            renderer.UIText(lines[i], { box.x + theme.padding, box.y + theme.padding + i * lineHeight },
                            theme.text);
        }
    }

    bool UI::IsHovered(const Rect& rect) const
    {
        if (!rect.Contains(m_mouse)) return false;
        // Everything behind a dialog is inert until the dialog is answered.
        if (m_hasModal && !m_modal.Contains(m_mouse)) return false;
        // A widget inside a scroll view is only hovered while visible.
        for (const Rect& clip : m_clipRegions)
        {
            if (!clip.Contains(m_mouse)) return false;
        }
        return true;
    }

    void UI::BlockMouse(const Rect& rect)
    {
        if (rect.Contains(m_mouse)) m_wantsMouseNext = true;
    }

    f32 UI::Transition(const std::string& id, bool active, f32 seconds)
    {
        f32& value = m_transitions[id];
        const f32 step = seconds > 0.0f ? Renderer::Get().DeltaTime() / seconds : 1.0f;
        value = Clamp01(value + (active ? step : -step));
        // Ease-out so the movement decelerates into place.
        return 1.0f - (1.0f - value) * (1.0f - value);
    }

    f32 UI::SlideIn(const std::string& id, bool active, f32 distance, f32 seconds)
    {
        return (1.0f - Transition(id, active, seconds)) * distance;
    }

    void UI::RestartTransition(const std::string& id)
    {
        m_transitions[id] = 0.0f;
    }

    void UI::Panel(const Rect& rect, const std::string& title)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        BlockMouse(rect);

        renderer.UIRect(rect, theme.panel.WithAlpha(0.96f));
        renderer.UIRectOutline(rect, theme.border, theme.borderThickness);

        if (!title.empty())
        {
            const Rect header{ rect.x, rect.y, rect.w, theme.headerHeight };
            renderer.UIRect(header, theme.panelHeader);
            renderer.UIRect({ rect.x, header.Bottom() - 1.0f, rect.w, 1.0f }, theme.border);
            renderer.UIText(title, { header.x + theme.padding,
                                     header.y + (header.h - renderer.TextHeight()) * 0.5f }, theme.accent);
        }
    }

    void UI::PanelBody(const Rect& rect)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        BlockMouse(rect);
        renderer.UIRect(rect, theme.panelAlt.WithAlpha(0.9f));
        renderer.UIRectOutline(rect, theme.border, theme.borderThickness);
    }

    void UI::Separator(const Rect& rect)
    {
        Renderer::Get().UIRect({ rect.x, rect.Center().y, rect.w, 1.0f }, Theme::Get().border);
    }

    Rect UI::BeginScroll(const Rect& rect, f32 contentHeight, f32& scroll)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        BlockMouse(rect);

        // Last frame measured how tall the content really was; trust that over the estimate.
        const auto remembered = m_scrollExtents.find(&scroll);
        if (remembered != m_scrollExtents.end()) contentHeight = std::max(contentHeight, remembered->second);

        const f32 maximum = std::max(0.0f, contentHeight - rect.h);
        if (IsHovered(rect))
        {
            const f32 wheel = Input::Get().WheelDelta();
            if (wheel != 0.0f) scroll -= wheel * theme.scrollSpeed;
        }
        scroll = std::clamp(scroll, 0.0f, maximum);

        renderer.PushClip(rect);
        m_clipRegions.push_back(rect);
        m_scrollKeys.push_back(&scroll);
        m_scrollTops.push_back(rect.y - scroll);

        if (maximum > 0.0f)
        {
            const f32 barWidth = 4.0f;
            const f32 trackX = rect.Right() - barWidth - 1.0f;
            renderer.UIRect({ trackX, rect.y, barWidth, rect.h }, theme.panelAlt);
            const f32 handleHeight = std::max(24.0f, rect.h * (rect.h / contentHeight));
            const f32 handleY = rect.y + (rect.h - handleHeight) * (scroll / maximum);
            renderer.UIRect({ trackX, handleY, barWidth, handleHeight }, theme.borderStrong);
        }

        return { rect.x, rect.y - scroll, rect.w - (maximum > 0.0f ? 8.0f : 0.0f), contentHeight };
    }

    void UI::EndScroll(f32 contentBottom)
    {
        if (!m_scrollKeys.empty())
        {
            if (contentBottom > 0.0f)
            {
                // A little slack so the last row is not flush with the bottom edge.
                m_scrollExtents[m_scrollKeys.back()] =
                    std::max(0.0f, contentBottom - m_scrollTops.back()) + 8.0f;
            }
            m_scrollKeys.pop_back();
            m_scrollTops.pop_back();
        }

        Renderer::Get().PopClip();
        if (!m_clipRegions.empty()) m_clipRegions.pop_back();
    }

    void UI::PlayPressSound()
    {
        AudioSystem::Get().PlayClick();
    }

    bool UI::Button(const Rect& rect, const std::string& label, ButtonState state)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        const u32 id = HashId(label, rect);
        BlockMouse(rect);

        // A red button is still a live button - hovering it tells the player what it would
        // cost - but pressing it buys nothing, so it never reports a click.
        const bool live = state != ButtonState::Disabled;
        const bool hovered = live && IsHovered(rect);
        if (hovered) m_hotId = id;
        if (hovered && m_mousePressed) m_activeId = id;

        const bool held = live && m_activeId == id && m_mouseDown;
        const bool clicked = state == ButtonState::Ready && hovered &&
                             m_mouseReleased && m_activeId == id;

        Color fill = theme.panelAlt;
        Color outline = theme.border;
        Color text = theme.text;

        switch (state)
        {
        case ButtonState::Ready:
            if (held) fill = Mix(theme.panelAlt, theme.accent, 0.35f);
            else if (hovered) fill = Mix(theme.panelAlt, theme.accent, 0.16f);
            if (hovered) outline = theme.accent;
            break;

        case ButtonState::Unaffordable:
            fill = Mix(theme.panelAlt, theme.negative, hovered ? 0.34f : 0.22f);
            outline = theme.negative;
            text = Mix(theme.text, theme.negative, 0.45f);
            break;

        case ButtonState::Disabled:
            fill = theme.panel.Scaled(0.8f);
            text = theme.textDim;
            break;
        }

        renderer.UIRect(rect, fill);
        renderer.UIRectOutline(rect, outline, theme.borderThickness);
        renderer.UITextCentered(label, rect, text);

        if (clicked) PlayPressSound();
        return clicked;
    }

    bool UI::InvisibleButton(const Rect& rect, const std::string& id, bool enabled)
    {
        const u32 hash = HashId(id, rect);
        BlockMouse(rect);

        const bool hovered = enabled && IsHovered(rect);
        if (hovered && m_mousePressed) m_activeId = hash;

        const bool clicked = enabled && hovered && m_mouseReleased && m_activeId == hash;
        if (clicked) PlayPressSound();
        return clicked;
    }

    bool UI::IconButton(const Rect& rect, SpriteId sprite, const std::string& tooltip, bool enabled)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        const u32 id = HashId(tooltip, rect);
        BlockMouse(rect);

        const bool hovered = enabled && IsHovered(rect);
        if (hovered && m_mousePressed) m_activeId = id;
        const bool clicked = enabled && hovered && m_mouseReleased && m_activeId == id;

        renderer.UIRect(rect, hovered ? Mix(theme.panelAlt, theme.accent, 0.2f) : theme.panelAlt);
        renderer.UIRectOutline(rect, hovered ? theme.accent : theme.border, theme.borderThickness);
        renderer.UISprite(sprite, rect.Inset(3.0f), enabled ? theme.text : theme.textDim);
        if (hovered && !tooltip.empty()) Tooltip(tooltip);

        if (clicked) PlayPressSound();
        return clicked;
    }

    bool UI::ListItem(const Rect& rect, const std::string& label, bool selected, const Color& accentStripe)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        const u32 id = HashId(label, rect);
        BlockMouse(rect);

        const bool hovered = IsHovered(rect);
        if (hovered && m_mousePressed) m_activeId = id;
        const bool clicked = hovered && m_mouseReleased && m_activeId == id;

        if (selected) renderer.UIRect(rect, Mix(theme.panelAlt, theme.accent, 0.25f));
        else if (hovered) renderer.UIRect(rect, theme.panelAlt);

        if (accentStripe.a > 0.0f)
        {
            renderer.UIRect({ rect.x, rect.y, 3.0f, rect.h }, accentStripe);
        }
        renderer.UIText(label, { rect.x + theme.padding + (accentStripe.a > 0.0f ? 4.0f : 0.0f),
                                 rect.y + (rect.h - renderer.TextHeight()) * 0.5f },
                        selected ? theme.textStrong : theme.text);
        if (selected) renderer.UIRectOutline(rect, theme.accent, 1.0f);

        if (clicked) PlayPressSound();
        return clicked;
    }

    bool UI::Toggle(const Rect& rect, const std::string& label, bool& value)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        const u32 id = HashId(label, rect);
        BlockMouse(rect);

        const bool hovered = IsHovered(rect);
        if (hovered && m_mousePressed) m_activeId = id;
        const bool clicked = hovered && m_mouseReleased && m_activeId == id;
        if (clicked) { value = !value; PlayPressSound(); }

        const f32 box = std::min(rect.h - 6.0f, 16.0f);
        const Rect checkbox{ rect.x + 2.0f, rect.y + (rect.h - box) * 0.5f, box, box };
        renderer.UIRect(checkbox, value ? theme.accent : theme.panelAlt);
        renderer.UIRectOutline(checkbox, hovered ? theme.accent : theme.border, 1.0f);
        if (value) renderer.UIRect(checkbox.Inset(4.0f), theme.panel);

        renderer.UIText(label, { checkbox.Right() + theme.padding,
                                 rect.y + (rect.h - renderer.TextHeight()) * 0.5f }, theme.text);
        return clicked;
    }

    bool UI::Stepper(const Rect& rect, const std::string& label, i32& value, i32 minimum, i32 maximum)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        BlockMouse(rect);

        const f32 buttonWidth = 22.0f;
        const Rect minus{ rect.Right() - buttonWidth * 2.0f - 44.0f, rect.y, buttonWidth, rect.h };
        const Rect display{ minus.Right(), rect.y, 44.0f, rect.h };
        const Rect plus{ display.Right(), rect.y, buttonWidth, rect.h };

        renderer.UIText(label, { rect.x, rect.y + (rect.h - renderer.TextHeight()) * 0.5f }, theme.text);

        bool changed = false;
        if (Button(minus, "-", value > minimum)) { value = std::max(minimum, value - 1); changed = true; }
        if (Button(plus, "+", value < maximum)) { value = std::min(maximum, value + 1); changed = true; }

        renderer.UIRect(display, theme.panel);
        renderer.UIRectOutline(display, theme.border, 1.0f);
        renderer.UITextCentered(std::to_string(value), display, theme.textStrong);
        return changed;
    }

    bool UI::Slider(const Rect& rect, const std::string& label, f32& value, f32 minimum, f32 maximum)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        const u32 id = HashId(label, rect);
        BlockMouse(rect);

        const f32 labelWidth = label.empty() ? 0.0f : renderer.TextWidth(label) + theme.padding;
        if (!label.empty())
        {
            renderer.UIText(label, { rect.x, rect.y + (rect.h - renderer.TextHeight()) * 0.5f }, theme.text);
        }

        const Rect track{ rect.x + labelWidth, rect.y + rect.h * 0.5f - 3.0f, rect.w - labelWidth, 6.0f };
        const bool hovered = IsHovered(rect);
        if (hovered && m_mousePressed) m_activeId = id;

        bool changed = false;
        if (m_activeId == id && m_mouseDown && track.w > 0.0f)
        {
            const f32 t = Clamp01((m_mouse.x - track.x) / track.w);
            const f32 next = minimum + (maximum - minimum) * t;
            if (next != value) { value = next; changed = true; }
        }

        const f32 fraction = maximum > minimum ? Clamp01((value - minimum) / (maximum - minimum)) : 0.0f;
        renderer.UIRect(track, theme.panelAlt);
        renderer.UIRect({ track.x, track.y, track.w * fraction, track.h }, theme.accent);
        const Rect handle{ track.x + track.w * fraction - 4.0f, rect.y + 2.0f, 8.0f, rect.h - 4.0f };
        renderer.UIRect(handle, hovered ? theme.selection : theme.borderStrong);
        return changed;
    }

    size_t UI::CountCharacters(const std::string& utf8)
    {
        size_t count = 0;
        for (char c : utf8)
        {
            // Continuation bytes belong to the letter before them.
            if ((static_cast<u8>(c) & 0xC0) != 0x80) ++count;
        }
        return count;
    }

    bool UI::TextField(const Rect& rect, const std::string& id, std::string& value, size_t maxLength)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        Input& input = Input::Get();
        BlockMouse(rect);

        const bool hovered = IsHovered(rect);
        if (hovered && m_mousePressed) m_activeTextField = id;

        const bool focused = m_activeTextField == id;
        bool changed = false;

        if (focused)
        {
            // Typed text arrives as UTF-8, and a Cyrillic letter is two bytes. Appending it
            // byte by byte against a byte limit could cut a letter in half - which is what
            // made the field look as though it refused Ukrainian - so whole sequences go in
            // at once and the limit counts letters, not bytes.
            const std::string& typed = input.TypedText();
            for (size_t i = 0; i < typed.size();)
            {
                const u8 lead = static_cast<u8>(typed[i]);
                size_t length = 1;
                if ((lead & 0xE0) == 0xC0) length = 2;
                else if ((lead & 0xF0) == 0xE0) length = 3;
                else if ((lead & 0xF8) == 0xF0) length = 4;
                length = std::min(length, typed.size() - i);

                if (CountCharacters(value) < maxLength)
                {
                    value.append(typed, i, length);
                    changed = true;
                }
                i += length;
            }
            if (input.WasKeyPressed(Key::Backspace) && !value.empty())
            {
                // Remove a whole UTF-8 sequence, not a single byte.
                size_t cut = value.size() - 1;
                while (cut > 0 && (static_cast<u8>(value[cut]) & 0xC0) == 0x80) --cut;
                value.erase(cut);
                changed = true;
            }
            if (input.WasKeyPressed(Key::Enter) || input.WasKeyPressed(Key::Escape))
            {
                m_activeTextField.clear();
            }
        }

        renderer.UIRect(rect, theme.panel);
        renderer.UIRectOutline(rect, focused ? theme.accent : theme.border, 1.0f);

        const f32 textY = rect.y + (rect.h - renderer.TextHeight()) * 0.5f;
        renderer.PushClip(rect);
        renderer.UIText(value, { rect.x + 6.0f, textY }, theme.text);
        if (focused && std::fmod(m_caretTimer, 1.0f) < 0.5f)
        {
            const f32 caretX = rect.x + 6.0f + renderer.TextWidth(value) + 1.0f;
            renderer.UIRect({ caretX, textY, 1.0f, renderer.TextHeight() }, theme.accent);
        }
        renderer.PopClip();
        return changed;
    }

    void UI::Label(const Rect& rect, const std::string& text, const Color& color, f32 scale)
    {
        Renderer& renderer = Renderer::Get();
        renderer.UIText(text, { rect.x, rect.y + (rect.h - renderer.TextHeight(scale)) * 0.5f }, color, scale);
    }

    void UI::LabelCentered(const Rect& rect, const std::string& text, const Color& color, f32 scale)
    {
        Renderer::Get().UITextCentered(text, rect, color, scale);
    }

    void UI::LabelRight(const Rect& rect, const std::string& text, const Color& color, f32 scale)
    {
        Renderer& renderer = Renderer::Get();
        const f32 width = renderer.TextWidth(text, scale);
        renderer.UIText(text, { rect.Right() - width, rect.y + (rect.h - renderer.TextHeight(scale)) * 0.5f },
                        color, scale);
    }

    void UI::KeyValue(const Rect& rect, const std::string& key, const std::string& value, const Color& valueColor)
    {
        const Theme& theme = Theme::Get();
        Label(rect, key, theme.textDim);
        LabelRight(rect, value, valueColor);
    }

    void UI::ProgressBar(const Rect& rect, f32 value01, const Color& fill, const std::string& overlay)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        renderer.UIRect(rect, theme.panel);
        renderer.UIRect({ rect.x, rect.y, rect.w * Clamp01(value01), rect.h }, fill);
        renderer.UIRectOutline(rect, theme.border, 1.0f);
        if (!overlay.empty()) renderer.UITextCentered(overlay, rect, theme.textStrong);
    }

    void UI::Chip(const Rect& rect, const std::string& text, const Color& color)
    {
        Renderer& renderer = Renderer::Get();
        renderer.UIRect(rect, color.WithAlpha(0.25f));
        renderer.UIRectOutline(rect, color, 1.0f);
        renderer.UITextCentered(text, rect, color);
    }

    bool UI::ColorSwatch(const Rect& rect, const Color& color, bool selected, const std::string& id)
    {
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();

        const bool clicked = InvisibleButton(rect, id);
        const bool hovered = IsHovered(rect);

        renderer.UIRect(rect, color);
        renderer.UIRectOutline(rect, selected ? theme.textStrong : (hovered ? theme.accent : theme.border),
                               selected ? 2.0f : 1.0f);
        return clicked;
    }

    std::vector<std::string> UI::WrapText(const std::string& text, f32 maxWidth, f32 scale)
    {
        const Renderer& renderer = Renderer::Get();
        std::vector<std::string> lines;
        std::string current;
        std::string word;

        auto flushWord = [&]()
        {
            if (word.empty()) return;
            // Measure the candidate before committing, so a long word starts its own line
            // instead of overflowing the one it was appended to.
            const std::string candidate = current.empty() ? word : current + " " + word;
            if (!current.empty() && renderer.TextWidth(candidate, scale) > maxWidth)
            {
                lines.push_back(current);
                current = word;
            }
            else
            {
                current = candidate;
            }
            word.clear();
        };

        for (char c : text)
        {
            if (c == '\n') { flushWord(); lines.push_back(current); current.clear(); }
            else if (c == ' ') flushWord();
            else word += c;
        }
        flushWord();
        if (!current.empty()) lines.push_back(current);
        return lines;
    }

    f32 UI::Paragraph(const Rect& rect, const std::string& text, const Color& color, f32 scale)
    {
        Renderer& renderer = Renderer::Get();
        const f32 lineHeight = renderer.TextHeight(scale) + 2.0f;

        f32 y = rect.y;
        for (const std::string& line : WrapText(text, rect.w, scale))
        {
            renderer.UIText(line, { rect.x, y }, color, scale);
            y += lineHeight;
        }
        return y - rect.y;
    }

    void UI::Tooltip(const std::string& text)
    {
        if (!text.empty()) m_tooltip = text;
    }

    void UI::TooltipIfHovered(const Rect& rect, const std::string& text)
    {
        if (IsHovered(rect)) Tooltip(text);
    }
}
