#include "Theme.h"
#include "../Core/Config.h"

#include <cstdlib>

#include <algorithm>
#include <cmath>

namespace woc
{
    namespace
    {
        Color ReadColor(const Json& palette, const char* key, const Color& fallback)
        {
            if (!palette.Has(key)) return fallback;
            const u32 hex = static_cast<u32>(std::strtoul(palette[key].AsString().c_str(), nullptr, 16));
            return Color::FromRGB(hex);
        }
    }

    void Theme::Load()
    {
        const Json& doc = ConfigManager::Get().UI();
        const Json& palette = doc["palette"];

        background   = ReadColor(palette, "background",   Color::FromRGB(0x0D1014));
        panel        = ReadColor(palette, "panel",        Color::FromRGB(0x161B22));
        panelAlt     = ReadColor(palette, "panelAlt",     Color::FromRGB(0x1D242E));
        panelHeader  = ReadColor(palette, "panelHeader",  Color::FromRGB(0x222C38));
        border       = ReadColor(palette, "border",       Color::FromRGB(0x3A4652));
        borderStrong = ReadColor(palette, "borderStrong", Color::FromRGB(0x5D6C7A));
        text         = ReadColor(palette, "text",         Color::FromRGB(0xD8DEE6));
        textDim      = ReadColor(palette, "textDim",      Color::FromRGB(0x8B97A4));
        textStrong   = ReadColor(palette, "textStrong",   Color::FromRGB(0xFFFFFF));
        accent       = ReadColor(palette, "accent",       Color::FromRGB(0xC9A227));
        accentDim    = ReadColor(palette, "accentDim",    Color::FromRGB(0x8A7020));
        positive     = ReadColor(palette, "positive",     Color::FromRGB(0x5AA860));
        negative     = ReadColor(palette, "negative",     Color::FromRGB(0xC05046));
        warning      = ReadColor(palette, "warning",      Color::FromRGB(0xD2933A));
        selection    = ReadColor(palette, "selection",    Color::FromRGB(0xE4D08A));
        shadow       = ReadColor(palette, "shadow",       Color::FromRGB(0x05070A));

        const Json& metrics = doc["metrics"];
        padding         = metrics["padding"].AsFloat(padding);
        rowHeight       = metrics["rowHeight"].AsFloat(rowHeight);
        headerHeight    = metrics["headerHeight"].AsFloat(headerHeight);
        buttonHeight    = metrics["buttonHeight"].AsFloat(buttonHeight);
        sidebarWidth    = metrics["sidebarWidth"].AsFloat(sidebarWidth);
        topBarHeight    = metrics["topBarHeight"].AsFloat(topBarHeight);
        bottomBarHeight = metrics["bottomBarHeight"].AsFloat(bottomBarHeight);
        tooltipMaxWidth = metrics["tooltipMaxWidth"].AsFloat(tooltipMaxWidth);
        scrollSpeed     = metrics["scrollSpeed"].AsFloat(scrollSpeed);
        borderThickness = metrics["borderThickness"].AsFloat(borderThickness);

        m_labels.clear();
        for (const auto& [key, value] : doc["labels"].AsObject())
        {
            m_labels[key] = value.AsString(key);
        }
    }

    void Theme::SetScale(f32 scale)
    {
        m_scale = std::clamp(scale, 0.6f, 2.0f);

        // Re-read first, so scaling is always applied to the designed sizes rather than
        // compounding on top of the last scale.
        Load();

        for (f32* metric : { &padding, &rowHeight, &headerHeight, &buttonHeight,
                             &sidebarWidth, &topBarHeight, &bottomBarHeight,
                             &tooltipMaxWidth, &scrollSpeed })
        {
            *metric = std::round(*metric * m_scale);
        }
        // Not the border: a hairline is a hairline at any size, and rounding it up makes
        // the whole interface look heavy-handed.
    }

    const std::string& Theme::Label(const std::string& key) const
    {
        const auto it = m_labels.find(key);
        return it != m_labels.end() ? it->second : key;
    }
}
