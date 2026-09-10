#version 450

// One large quad lying on the water plane. It reaches far beyond the map, so the world
// has no visible edge: the island simply sits in an open sea.

layout(set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 proj;
    mat4 uiProj;
    vec4 params;      // x = seconds
} g;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec2 vWorld;

void main()
{
    vWorld = inPosition.xy;
    gl_Position = g.proj * g.view * vec4(inPosition, 1.0);
}
