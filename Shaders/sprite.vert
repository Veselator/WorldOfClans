#version 450

// Screen-aligned billboards standing on the 3D terrain: the 2.5D look. The base quad is
// generated from gl_VertexIndex, so only the per-instance stream touches memory.

layout(set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 proj;
    mat4 uiProj;
    vec4 params;
} g;

layout(location = 0) in vec3 iWorldPosition;   // ground point the sprite stands on
layout(location = 1) in vec2 iSize;            // world-space width and height
layout(location = 2) in vec4 iUVRect;          // xy = uv min, zw = uv max
layout(location = 3) in vec4 iColor;           // tint, multiplied with the texel
layout(location = 4) in vec4 iParams;          // x = vertical anchor, y = sort bias (CPU only), z = flash

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vFlash;

const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main()
{
    vec2 corner = kCorners[gl_VertexIndex];

    // corner.y == 0 is the top of the sprite in texture space, so flip for the world offset.
    // w shifts the billboard sideways on screen, so a row of small marks can be laid out
    // over one point without each needing a world position of its own.
    vec2 offset = vec2((corner.x - 0.5) * iSize.x + iParams.w,
                       (1.0 - corner.y - iParams.x) * iSize.y);

    vec4 viewPosition = g.view * vec4(iWorldPosition, 1.0);
    viewPosition.xy += offset;

    gl_Position = g.proj * viewPosition;

    vUV = mix(iUVRect.xy, iUVRect.zw, corner);
    vColor = iColor;
    vFlash = iParams.z;
}
