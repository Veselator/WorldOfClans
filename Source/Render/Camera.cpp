#include "Camera.h"

namespace woc
{
    void Camera::Configure(f32 pitchDegrees, f32 minZoom, f32 maxZoom, f32 defaultZoom)
    {
        m_pitchDegrees = pitchDegrees;
        m_minZoom = minZoom;
        m_maxZoom = maxZoom;
        m_zoom = std::clamp(defaultZoom, minZoom, maxZoom);
    }

    Vec3 Camera::Forward() const
    {
        const f32 pitch = m_pitchDegrees * kDeg2Rad;
        const f32 yaw = m_yawDegrees * kDeg2Rad;
        const f32 horizontal = std::cos(pitch);

        // Looking down at the ground; the horizontal part spins with the yaw.
        return { -std::sin(yaw) * horizontal, std::cos(yaw) * horizontal, -std::sin(pitch) };
    }

    Vec3 Camera::Right() const
    {
        return Cross(Forward(), { 0.0f, 0.0f, 1.0f }).Normalized();
    }

    Vec3 Camera::Up() const
    {
        return Cross(Right(), Forward());
    }

    Vec3 Camera::Eye() const
    {
        return ToWorld(m_focus, 0.0f) - Forward() * m_distance;
    }

    Mat4 Camera::View() const
    {
        return Mat4::LookAt(Eye(), ToWorld(m_focus, 0.0f), { 0.0f, 0.0f, 1.0f });
    }

    Mat4 Camera::Projection() const
    {
        const f32 halfWidth = m_viewportWidth * 0.5f / m_zoom;
        const f32 halfHeight = m_viewportHeight * 0.5f / m_zoom;
        return Mat4::OrthoView(halfWidth, halfHeight, 1.0f, m_distance * 2.0f + 4000.0f);
    }

    void Camera::SetZoom(f32 zoom)
    {
        m_zoom = std::clamp(zoom, m_minZoom, m_maxZoom);
    }

    void Camera::ZoomAt(f32 factor, const Vec2& screenPoint)
    {
        const Vec2 before = ScreenToMap(screenPoint);
        SetZoom(m_zoom * factor);
        const Vec2 after = ScreenToMap(screenPoint);
        m_focus += before - after;
        ClampToBounds();
    }

    void Camera::PanScreenRelative(const Vec2& screenDelta)
    {
        // Screen right and screen "up the map" projected onto the ground plane.
        const Vec3 right = Right();
        const Vec3 forward = Forward();
        const Vec2 groundRight{ right.x, -right.y };
        Vec2 groundForward{ forward.x, -forward.y };
        if (groundForward.LengthSq() > 1e-6f) groundForward = groundForward.Normalized();

        // screenDelta.y is in screen coordinates, so moving "up" walks along +forward.
        m_focus += groundRight * screenDelta.x - groundForward * screenDelta.y;
        ClampToBounds();
    }

    void Camera::ClampToBounds()
    {
        if (m_boundsMax.x <= m_boundsMin.x) return;
        m_focus.x = std::clamp(m_focus.x, m_boundsMin.x, m_boundsMax.x);
        m_focus.y = std::clamp(m_focus.y, m_boundsMin.y, m_boundsMax.y);
    }

    Vec2 Camera::MapToScreen(const Vec2& mapPosition, f32 height) const
    {
        const Mat4 viewProj = Projection() * View();
        const Vec3 world = ToWorld(mapPosition, height);
        const Vec4 clip = viewProj.Transform({ world.x, world.y, world.z, 1.0f });
        return { (clip.x * 0.5f + 0.5f) * m_viewportWidth,
                 (clip.y * 0.5f + 0.5f) * m_viewportHeight };
    }

    Vec2 Camera::ScreenToMap(const Vec2& screenPoint, f32 height) const
    {
        const Vec3 forward = Forward();
        if (forward.z > -1e-4f) return m_focus;   // degenerate: camera looking at the horizon

        const f32 halfWidth = m_viewportWidth * 0.5f / m_zoom;
        const f32 halfHeight = m_viewportHeight * 0.5f / m_zoom;

        const f32 ndcX = (screenPoint.x / m_viewportWidth) * 2.0f - 1.0f;
        const f32 ndcY = (screenPoint.y / m_viewportHeight) * 2.0f - 1.0f;

        // Undo the projection: OrthoView flips the vertical axis.
        const f32 viewX = ndcX * halfWidth;
        const f32 viewY = -ndcY * halfHeight;

        const Vec3 right = Right();
        const Vec3 up = Up();
        const Vec3 eye = Eye();

        // Walk along the view ray until it reaches the requested ground height.
        const Vec3 origin = eye + right * viewX + up * viewY;
        const f32 t = (origin.z - height) / -forward.z;

        const Vec3 world = origin + forward * t;
        return { world.x, -world.y };
    }

    Vec2 Camera::VisibleHalfExtent() const
    {
        const f32 pitch = m_pitchDegrees * kDeg2Rad;
        const f32 sinP = std::max(std::sin(pitch), 0.15f);

        // With rotation the visible footprint is a diamond; the bounding box of it is what
        // culling wants, so take the larger of the two axes for both.
        const f32 halfWidth = m_viewportWidth * 0.5f / m_zoom;
        const f32 halfDepth = m_viewportHeight * 0.5f / (m_zoom * sinP);
        const f32 extent = std::max(halfWidth, halfDepth);
        return { extent, extent };
    }
}
