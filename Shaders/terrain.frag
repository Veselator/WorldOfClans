#version 450

// Composites the map layers: base terrain colour, tiled forest and field sprite masks,
// then the dynamic realm ownership tint with a highlighted frontier.

layout(set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 proj;
    mat4 uiProj;
    vec4 params;
} g;

layout(set = 1, binding = 0) uniform sampler2D uTerrain;   // base colour map
layout(set = 1, binding = 1) uniform sampler2D uTrees;     // r = forest density 0..1
layout(set = 1, binding = 2) uniform sampler2D uFields;    // r = tilled field coverage
layout(set = 1, binding = 3) uniform sampler2D uOwner;     // r = owner slot * (1/255)
layout(set = 1, binding = 4) uniform sampler2D uAtlas;     // shared sprite sheet
layout(set = 1, binding = 5) uniform Palette
{
    vec4 colors[32];   // slot 0 is "unclaimed"
} palette;

layout(push_constant) uniform Push
{
    vec4 forestRect;   // xy = uv min, zw = uv max inside the atlas
    vec4 fieldRect;
    vec4 settings;     // x = forest tiling, y = field tiling, z = border width, w = owner tint
    vec4 texel;        // xy = 1/ownerSize, zw = sun direction xy
    vec4 water;        // rgb = the palette colour of water, a = match tolerance
    vec4 flags;        // x = draw realm borders, yzw = unused
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorld;

layout(location = 0) out vec4 outColor;

vec4 SampleTile(vec4 rect, vec2 uv, float tiling)
{
    vec2 local = fract(uv * tiling);
    return textureLod(uAtlas, mix(rect.xy, rect.zw, local), 0.0);
}

void main()
{
    vec3 color = textureLod(uTerrain, vUV, 0.0).rgb;

    // Water is not drawn here at all: the sea plane underneath already carries the waves,
    // so sea and rivers move as one surface and the map has no hard edge.
    if (distance(color, pc.water.rgb) < pc.water.a) discard;

    // --- forest ------------------------------------------------------------------------
    float forest = textureLod(uTrees, vUV, 0.0).r;
    if (forest > 0.02)
    {
        vec4 tile = SampleTile(pc.forestRect, vUV, pc.settings.x);
        color = mix(color, tile.rgb, tile.a * clamp(forest * 1.6, 0.0, 1.0));
    }

    // --- tilled fields ------------------------------------------------------------------
    float field = textureLod(uFields, vUV, 0.0).r;
    if (field > 0.02)
    {
        vec4 tile = SampleTile(pc.fieldRect, vUV, pc.settings.y);
        color = mix(color, tile.rgb, tile.a * clamp(field, 0.0, 1.0) * 0.85);
    }

    // --- realm ownership ----------------------------------------------------------------
    float ownerRaw = textureLod(uOwner, vUV, 0.0).r;
    int ownerSlot = int(ownerRaw * 255.0 + 0.5);
    if (ownerSlot > 0 && pc.flags.x > 0.5)
    {
        vec4 realm = palette.colors[min(ownerSlot, 31)];
        color = mix(color, realm.rgb, pc.settings.w * realm.a);

        // A frontier is any texel whose neighbours belong to somebody else: this is what
        // makes borders follow the coverage field instead of a drawn political map.
        float edge = 0.0;
        for (int i = -1; i <= 1; ++i)
        {
            for (int j = -1; j <= 1; ++j)
            {
                if (i == 0 && j == 0) continue;
                float n = textureLod(uOwner, vUV + vec2(float(i), float(j)) * pc.texel.xy * pc.settings.z, 0.0).r;
                edge = max(edge, abs(n - ownerRaw) > 0.001 ? 1.0 : 0.0);
            }
        }
        color = mix(color, realm.rgb, edge * 0.92);
    }

    // --- lighting -----------------------------------------------------------------------
    vec3 sun = normalize(vec3(pc.texel.z, pc.texel.w, 0.85));
    float lambert = clamp(dot(normalize(vNormal), sun), 0.0, 1.0);
    float shade = 0.62 + 0.45 * lambert;
    outColor = vec4(color * shade, 1.0);
}
