// Math.h - minimal linear algebra used by the renderer, camera and simulation.
#pragma once

#include "Types.h"
#include <cmath>
#include <algorithm>

namespace woc
{
    inline constexpr f32 kPi = 3.14159265358979323846f;
    inline constexpr f32 kDeg2Rad = kPi / 180.0f;
    inline constexpr f32 kRad2Deg = 180.0f / kPi;

    inline f32 Clamp01(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    inline f32 Lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

    struct Vec2
    {
        f32 x = 0.0f, y = 0.0f;
        constexpr Vec2() = default;
        constexpr Vec2(f32 xx, f32 yy) : x(xx), y(yy) {}

        Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
        Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
        Vec2 operator*(f32 s) const { return { x * s, y * s }; }
        Vec2 operator/(f32 s) const { return { x / s, y / s }; }
        Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
        Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
        Vec2& operator*=(f32 s) { x *= s; y *= s; return *this; }
        bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

        f32 LengthSq() const { return x * x + y * y; }
        f32 Length() const { return std::sqrt(LengthSq()); }
        Vec2 Normalized() const { const f32 l = Length(); return l > 1e-6f ? Vec2{ x / l, y / l } : Vec2{}; }
    };

    inline f32 Distance(const Vec2& a, const Vec2& b) { return (a - b).Length(); }
    inline f32 DistanceSq(const Vec2& a, const Vec2& b) { return (a - b).LengthSq(); }

    struct Vec3
    {
        f32 x = 0.0f, y = 0.0f, z = 0.0f;
        constexpr Vec3() = default;
        constexpr Vec3(f32 xx, f32 yy, f32 zz) : x(xx), y(yy), z(zz) {}

        Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        Vec3 operator*(f32 s) const { return { x * s, y * s, z * s }; }
        Vec3 operator-() const { return { -x, -y, -z }; }
        Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

        f32 Length() const { return std::sqrt(x * x + y * y + z * z); }
        Vec3 Normalized() const { const f32 l = Length(); return l > 1e-6f ? Vec3{ x / l, y / l, z / l } : Vec3{}; }
    };

    inline f32 Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    struct Vec4
    {
        f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
        constexpr Vec4() = default;
        constexpr Vec4(f32 xx, f32 yy, f32 zz, f32 ww) : x(xx), y(yy), z(zz), w(ww) {}
    };

    /// RGBA colour in linear 0..1 space.
    struct Color
    {
        f32 r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        constexpr Color() = default;
        constexpr Color(f32 rr, f32 gg, f32 bb, f32 aa = 1.0f) : r(rr), g(gg), b(bb), a(aa) {}

        static Color FromRGB(u32 hex, f32 alpha = 1.0f)
        {
            return { ((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha };
        }
        u32 ToRGB() const
        {
            const u32 rr = static_cast<u32>(Clamp01(r) * 255.0f + 0.5f);
            const u32 gg = static_cast<u32>(Clamp01(g) * 255.0f + 0.5f);
            const u32 bb = static_cast<u32>(Clamp01(b) * 255.0f + 0.5f);
            return (rr << 16) | (gg << 8) | bb;
        }
        Color WithAlpha(f32 alpha) const { return { r, g, b, alpha }; }
        Color Scaled(f32 s) const { return { r * s, g * s, b * s, a }; }
    };

    inline Color LerpColor(const Color& a, const Color& b, f32 t)
    {
        return { Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t) };
    }

    /// Column-major 4x4 matrix, laid out exactly as the shaders expect.
    struct Mat4
    {
        f32 m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

        static Mat4 Identity() { return {}; }

        Mat4 operator*(const Mat4& o) const
        {
            Mat4 r;
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row)
                {
                    f32 s = 0.0f;
                    for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * o.m[c * 4 + k];
                    r.m[c * 4 + row] = s;
                }
            return r;
        }

        Vec4 Transform(const Vec4& v) const
        {
            return {
                m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
                m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w
            };
        }

        static Mat4 Translate(const Vec3& t)
        {
            Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r;
        }

        static Mat4 Scale(const Vec3& s)
        {
            Mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r;
        }

        static Mat4 RotateX(f32 rad)
        {
            Mat4 r; const f32 c = std::cos(rad), s = std::sin(rad);
            r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r;
        }

        static Mat4 RotateY(f32 rad)
        {
            Mat4 r; const f32 c = std::cos(rad), s = std::sin(rad);
            r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r;
        }

        static Mat4 RotateZ(f32 rad)
        {
            Mat4 r; const f32 c = std::cos(rad), s = std::sin(rad);
            r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r;
        }

        /// Orthographic projection for screen-space work: depth maps to Vulkan's 0..1 range
        /// and +z travels into the screen. Pass bottom > top to get y-down screen coordinates.
        static Mat4 Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar)
        {
            Mat4 r;
            r.m[0]  = 2.0f / (right - left);
            r.m[5]  = 2.0f / (top - bottom);
            r.m[10] = 1.0f / (zFar - zNear);
            r.m[12] = -(right + left) / (right - left);
            r.m[13] = -(top + bottom) / (top - bottom);
            r.m[14] = -zNear / (zFar - zNear);
            return r;
        }

        /// Orthographic projection meant to follow a right-handed LookAt view matrix.
        /// The camera looks down -z, so view depth is negated to reach Vulkan's 0..1 range,
        /// and the vertical axis is flipped so world "up" ends up at the top of the screen.
        static Mat4 OrthoView(f32 halfWidth, f32 halfHeight, f32 zNear, f32 zFar)
        {
            Mat4 r;
            r.m[0]  =  1.0f / halfWidth;
            r.m[5]  = -1.0f / halfHeight;
            r.m[10] = -1.0f / (zFar - zNear);
            r.m[14] = -zNear / (zFar - zNear);
            return r;
        }

        static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
        {
            const Vec3 f = (target - eye).Normalized();
            const Vec3 s = Cross(f, up).Normalized();
            const Vec3 u = Cross(s, f);
            Mat4 r;
            r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;  r.m[12] = -Dot(s, eye);
            r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;  r.m[13] = -Dot(u, eye);
            r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] =  Dot(f, eye);
            return r;
        }
    };

    /// Axis-aligned rectangle in screen space; the UI layer's basic currency.
    struct Rect
    {
        f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        constexpr Rect() = default;
        constexpr Rect(f32 xx, f32 yy, f32 ww, f32 hh) : x(xx), y(yy), w(ww), h(hh) {}

        f32 Right() const { return x + w; }
        f32 Bottom() const { return y + h; }
        Vec2 Center() const { return { x + w * 0.5f, y + h * 0.5f }; }
        bool Contains(const Vec2& p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
        Rect Inset(f32 d) const { return { x + d, y + d, w - 2 * d, h - 2 * d }; }
    };

    /// Integer grid coordinate; used pervasively by the map, coverage and pathfinding code.
    struct Coord
    {
        i32 x = 0, y = 0;
        constexpr Coord() = default;
        constexpr Coord(i32 xx, i32 yy) : x(xx), y(yy) {}
        bool operator==(const Coord& o) const { return x == o.x && y == o.y; }
        bool operator!=(const Coord& o) const { return !(*this == o); }
        Coord operator+(const Coord& o) const { return { x + o.x, y + o.y }; }
        Coord operator-(const Coord& o) const { return { x - o.x, y - o.y }; }
    };

    struct CoordHash
    {
        size_t operator()(const Coord& c) const noexcept
        {
            return (static_cast<size_t>(static_cast<u32>(c.x)) << 32) ^ static_cast<u32>(c.y);
        }
    };
}
