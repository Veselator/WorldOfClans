#include "Settings.h"

#include "Config.h"
#include "Json.h"
#include "Log.h"
#include "Paths.h"
#include "../Platform/Window.h"
#include "../Render/Renderer.h"
#include "../UI/Theme.h"
#include "../Game/Systems/Simulation.h"

namespace woc
{
    namespace
    {
        constexpr const char* kFile = "settings.json";
    }

    void Settings::Load()
    {
        ConfigManager& config = ConfigManager::Get();

        // Defaults come from the balance file; the player's file only overrides them.
        fullscreen = config.Bool("window/fullscreen", true);
        windowWidth = static_cast<u32>(config.Int("window/width", 1600));
        windowHeight = static_cast<u32>(config.Int("window/height", 900));
        vsync = config.Bool("render/vsync", true);
        rotateSpeed = config.Float("camera/rotateSpeed", 90.0f);
        edgeScroll = static_cast<f32>(config.Int("camera/edgeScrollMargin", 8));
        labelMinZoom = config.Float("render/labels/minZoom", 1.3f);
        uiScale = config.Float("render/uiScale", 1.0f);
        musicVolume = config.Float("audio/musicVolume", 0.45f);
        sfxVolume = config.Float("audio/sfxVolume", 0.8f);
        defaultSpeedIndex = config.Int("simulation/defaultSpeedIndex", 2);

        m_resolutions = {
            { 1280, 720 }, { 1366, 768 }, { 1600, 900 },
            { 1920, 1080 }, { 2560, 1440 }
        };

        const Json doc = Json::LoadFile(Paths::Get().Config(kFile));
        if (doc.IsNull()) return;

        fullscreen = doc["fullscreen"].AsBool(fullscreen);
        windowWidth = static_cast<u32>(doc["windowWidth"].AsInt(static_cast<i32>(windowWidth)));
        windowHeight = static_cast<u32>(doc["windowHeight"].AsInt(static_cast<i32>(windowHeight)));
        vsync = doc["vsync"].AsBool(vsync);
        rotateSpeed = doc["rotateSpeed"].AsFloat(rotateSpeed);
        edgeScroll = doc["edgeScroll"].AsFloat(edgeScroll);
        showFps = doc["showFps"].AsBool(showFps);
        showBorders = doc["showBorders"].AsBool(showBorders);
        showLabels = doc["showLabels"].AsBool(showLabels);
        labelMinZoom = doc["labelMinZoom"].AsFloat(labelMinZoom);
        uiScale = doc["uiScale"].AsFloat(uiScale);
        musicVolume = doc["musicVolume"].AsFloat(musicVolume);
        sfxVolume = doc["sfxVolume"].AsFloat(sfxVolume);
        defaultSpeedIndex = doc["defaultSpeedIndex"].AsInt(defaultSpeedIndex);

        WOC_LOG_INFO("Settings loaded from ", kFile);
    }

    void Settings::Save() const
    {
        Json doc = Json::MakeObject();
        doc["fullscreen"] = fullscreen;
        doc["windowWidth"] = static_cast<i64>(windowWidth);
        doc["windowHeight"] = static_cast<i64>(windowHeight);
        doc["vsync"] = vsync;
        doc["rotateSpeed"] = rotateSpeed;
        doc["edgeScroll"] = edgeScroll;
        doc["showFps"] = showFps;
        doc["showBorders"] = showBorders;
        doc["showLabels"] = showLabels;
        doc["labelMinZoom"] = labelMinZoom;
        doc["uiScale"] = uiScale;
        doc["musicVolume"] = musicVolume;
        doc["sfxVolume"] = sfxVolume;
        doc["defaultSpeedIndex"] = defaultSpeedIndex;

        if (!doc.SaveFile(Paths::Get().Config(kFile)))
        {
            WOC_LOG_ERROR("Could not write ", kFile);
        }
    }

    void Settings::Apply(Window& window)
    {
        window.SetFullscreen(fullscreen);
        if (!fullscreen) window.SetClientSize(windowWidth, windowHeight);

        Renderer& renderer = Renderer::Get();
        renderer.SetBordersVisible(showBorders);

        // The interface scales as one piece: the theme's metrics and every string drawn
        // through the renderer, so a panel and the text inside it grow together.
        Theme::Get().SetScale(uiScale);
        renderer.SetUIScale(uiScale);

        Camera& camera = renderer.GetCamera();
        camera.Configure(ConfigManager::Get().Float("camera/pitchDegrees", 46.0f),
                         camera.MinZoom(), camera.MaxZoom(), camera.Zoom());

        Simulation::Get().SetSpeedIndex(defaultSpeedIndex);
    }
}
