#version 450

layout(set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 proj;
    mat4 uiProj;
    vec4 params;
} g;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
    vUV = inUV;
    vColor = inColor;
    gl_Position = g.uiProj * vec4(inPosition, 0.0, 1.0);
}
