#version 450

// One pipeline serves every UI primitive; the bound texture and the mode push constant
// decide whether a quad is a solid fill, a glyph (single-channel coverage) or a sprite.

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform Push
{
    vec4 mode;   // x: 0 = solid, 1 = glyph, 2 = sprite
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    int mode = int(pc.mode.x + 0.5);

    if (mode == 1)
    {
        float coverage = textureLod(uTexture, vUV, 0.0).r;
        if (coverage <= 0.01) discard;
        outColor = vec4(vColor.rgb, vColor.a * coverage);
    }
    else if (mode == 2)
    {
        vec4 texel = textureLod(uTexture, vUV, 0.0);
        if (texel.a < 0.35) discard;
        outColor = vec4(texel.rgb * vColor.rgb, texel.a * vColor.a);
    }
    else
    {
        outColor = vColor;
    }
}
