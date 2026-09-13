#include "Settings.h"

#include <string>

#include "Config.h"
#include "Json.h"
#include "Log.h"
#include "Paths.h"
#include "../Audio/AudioSystem.h"
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

    const std::vector<i32>& Settings::AutosaveChoices()
    {
        static const std::vector<i32> kChoices = { 0, 7, 14, 30, 180, 360 };
        return kChoices;
    }

    std::string Settings::AutosaveLabel(i32 days)
    {
        switch (days)
        {
        case 7:   return "Щотижня";
        case 14:  return "Раз на два тижні";
        case 30:  return "Щомісяця";
        case 180: return "Раз на півроку";
        case 360: return "Щороку";
        default:  return "Ніколи";
        }
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
        masterVolume = config.Float("audio/masterVolume", 1.0f);
        musicVolume = config.Float("audio/musicVolume", 0.45f);
        sfxVolume = config.Float("audio/sfxVolume", 0.8f);
        defaultSpeedIndex = config.Int("simulation/defaultSpeedIndex", 2);
        autosaveDays = config.Int("simulation/autosaveDays", 0);

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
        smoothBorders = doc["smoothBorders"].AsBool(smoothBorders);
        showLabels = doc["showLabels"].AsBool(showLabels);
        labelMinZoom = doc["labelMinZoom"].AsFloat(labelMinZoom);
        uiScale = doc["uiScale"].AsFloat(uiScale);
        masterVolume = doc["masterVolume"].AsFloat(masterVolume);
        musicVolume = doc["musicVolume"].AsFloat(musicVolume);
        sfxVolume = doc["sfxVolume"].AsFloat(sfxVolume);
        defaultSpeedIndex = doc["defaultSpeedIndex"].AsInt(defaultSpeedIndex);
        autosaveDays = doc["autosaveDays"].AsInt(autosaveDays);

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
        doc["smoothBorders"] = smoothBorders;
        doc["showLabels"] = showLabels;
        doc["labelMinZoom"] = labelMinZoom;
        doc["uiScale"] = uiScale;
        doc["masterVolume"] = masterVolume;
        doc["musicVolume"] = musicVolume;
        doc["sfxVolume"] = sfxVolume;
        doc["defaultSpeedIndex"] = defaultSpeedIndex;
        doc["autosaveDays"] = autosaveDays;

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
        renderer.SetSmoothBorders(smoothBorders);

        AudioSystem::Get().SetMasterVolume(masterVolume);
        AudioSystem::Get().SetMusicVolume(musicVolume);
        AudioSystem::Get().SetSfxVolume(sfxVolume);

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
