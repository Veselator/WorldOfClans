#include "Settings.h"

#include "Config.h"
#include "Json.h"
#include "Log.h"
#include "Paths.h"
#include "../Platform/Window.h"
#include "../Render/Renderer.h"
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
        cameraPitch = config.Float("camera/pitchDegrees", 46.0f);
        rotateSpeed = config.Float("camera/rotateSpeed", 90.0f);
        edgeScroll = static_cast<f32>(config.Int("camera/edgeScrollMargin", 8));
        labelMinZoom = config.Float("render/labels/minZoom", 1.3f);
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
        cameraPitch = doc["cameraPitch"].AsFloat(cameraPitch);
        rotateSpeed = doc["rotateSpeed"].AsFloat(rotateSpeed);
        edgeScroll = doc["edgeScroll"].AsFloat(edgeScroll);
        showBorders = doc["showBorders"].AsBool(showBorders);
        showLabels = doc["showLabels"].AsBool(showLabels);
        labelMinZoom = doc["labelMinZoom"].AsFloat(labelMinZoom);
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
        doc["cameraPitch"] = cameraPitch;
        doc["rotateSpeed"] = rotateSpeed;
        doc["edgeScroll"] = edgeScroll;
        doc["showBorders"] = showBorders;
        doc["showLabels"] = showLabels;
        doc["labelMinZoom"] = labelMinZoom;
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

        Camera& camera = renderer.GetCamera();
        camera.Configure(cameraPitch, camera.MinZoom(), camera.MaxZoom(), camera.Zoom());

        Simulation::Get().SetSpeedIndex(defaultSpeedIndex);
    }
}
