// Theme.h - colours, metrics and interface labels loaded from Config/ui.json.
#pragma once

#include "../Core/Singleton.h"
#include "../Core/Math.h"
#include <unordered_map>

namespace woc
{
    class Theme final : public Singleton<Theme>
    {
        friend class Singleton<Theme>;
    public:
        void Load();

        Color background, panel, panelAlt, panelHeader, border, borderStrong;
        Color text, textDim, textStrong, accent, accentDim;
        Color positive, negative, warning, selection, shadow;

        f32 padding = 8.0f;
        f32 rowHeight = 22.0f;
        f32 headerHeight = 26.0f;
        f32 buttonHeight = 26.0f;
        f32 sidebarWidth = 320.0f;
        f32 topBarHeight = 30.0f;
        f32 bottomBarHeight = 96.0f;
        f32 tooltipMaxWidth = 360.0f;
        f32 scrollSpeed = 48.0f;
        f32 borderThickness = 1.0f;

        /// Localised interface string; falls back to the key itself when missing.
        const std::string& Label(const std::string& key) const;

    private:
        Theme() = default;
        ~Theme() = default;

        std::unordered_map<std::string, std::string> m_labels;
    };
}
