#version 450

// Terrain is a height-displaced grid; the fine detail comes from the map textures
// rather than from geometry, so a coarse mesh keeps full-resolution colour.

layout(set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 proj;
    mat4 uiProj;
    vec4 params;      // x = seconds, y = viewport width, z = viewport height, w = zoom
} g;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorld;

void main()
{
    vUV = inUV;
    vNormal = inNormal;
    vWorld = inPosition;
    gl_Position = g.proj * g.view * vec4(inPosition, 1.0);
}
