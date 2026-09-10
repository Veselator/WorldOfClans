#version 450

// Sprites in the atlas are drawn in greyscale so the tint can express ownership:
// bright pixels take the clan colour, dark outline pixels stay dark.

layout(set = 1, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vFlash;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texel = textureLod(uAtlas, vUV, 0.0);
    if (texel.a < 0.35) discard;

    vec3 tinted = texel.rgb * vColor.rgb;
    tinted = mix(tinted, vec3(1.0), vFlash);
    outColor = vec4(tinted, texel.a * vColor.a);
}
