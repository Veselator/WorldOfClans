// Camera.h - fixed-pitch, rotatable, zoomable orthographic camera.
//
// The elevation angle never changes (that is the game's chosen look); the player may spin
// the map around and zoom, and the camera also owns the map-to-world conversion, since map
// coordinates are image pixels with y growing downwards.
#pragma once

#include "../Core/Math.h"

namespace woc
{
    class Camera
    {
    public:
        void Configure(f32 pitchDegrees, f32 minZoom, f32 maxZoom, f32 defaultZoom);

        void SetViewport(f32 width, f32 height) { m_viewportWidth = width; m_viewportHeight = height; }
        void SetFocus(const Vec2& mapPosition) { m_focus = mapPosition; }
        void MoveFocus(const Vec2& deltaMapUnits) { m_focus += deltaMapUnits; }
        void SetBounds(const Vec2& min, const Vec2& max) { m_boundsMin = min; m_boundsMax = max; }
        void ClampToBounds();

        /// Pans along the screen axes rather than the world ones, so WASD keeps meaning
        /// "up the screen" no matter how far the map has been spun.
        void PanScreenRelative(const Vec2& screenDelta);

        /// Zooms about the cursor so the map point under the pointer stays put.
        void ZoomAt(f32 factor, const Vec2& screenPoint);
        void SetZoom(f32 zoom);

        void SetYawDegrees(f32 degrees) { m_yawDegrees = degrees; }
        void RotateBy(f32 degrees) { m_yawDegrees += degrees; }
        void ResetRotation() { m_yawDegrees = 0.0f; }
        f32 YawDegrees() const { return m_yawDegrees; }

        Vec2 Focus() const { return m_focus; }
        f32 Zoom() const { return m_zoom; }
        f32 MinZoom() const { return m_minZoom; }
        f32 MaxZoom() const { return m_maxZoom; }
        f32 PitchDegrees() const { return m_pitchDegrees; }

        Mat4 View() const;
        Mat4 Projection() const;

        /// Converts a map pixel plus terrain height into renderer world space.
        static Vec3 ToWorld(const Vec2& mapPosition, f32 height) { return { mapPosition.x, -mapPosition.y, height }; }

        /// Projects a map position to window pixels. This is what picking compares against,
        /// because it accounts for the terrain height an object stands on.
        Vec2 MapToScreen(const Vec2& mapPosition, f32 height = 0.0f) const;
        /// Inverse of MapToScreen for a given ground height.
        Vec2 ScreenToMap(const Vec2& screenPoint, f32 height = 0.0f) const;

        /// Half-extents of the visible area in map units, useful for culling.
        Vec2 VisibleHalfExtent() const;

        // --- camera basis, exposed for the water plane and for unprojection ------------------
        Vec3 Forward() const;
        Vec3 Right() const;
        Vec3 Up() const;
        Vec3 Eye() const;

    private:
        Vec2 m_focus{ 0.0f, 0.0f };
        Vec2 m_boundsMin{ 0.0f, 0.0f };
        Vec2 m_boundsMax{ 0.0f, 0.0f };
        f32 m_zoom = 1.0f;
        f32 m_minZoom = 0.25f;
        f32 m_maxZoom = 6.0f;
        f32 m_pitchDegrees = 45.0f;
        f32 m_yawDegrees = 0.0f;
        f32 m_viewportWidth = 1280.0f;
        f32 m_viewportHeight = 720.0f;
        f32 m_distance = 8000.0f;
    };
}
