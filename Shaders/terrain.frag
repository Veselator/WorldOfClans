#version 450

// Composites the map layers: base terrain colour, tiled forest and field sprite masks,
// then whichever thematic layer the player is looking at.
//
// That last layer is deliberately generic. The CPU fills one 8-bit mask with a slot per
// tile and one palette with a colour per slot; what a slot *means* - a realm, a single
// lord's zone of influence, a people, a faith - is the map mode, and the shader neither
// knows nor cares. Frontier detection then comes out right for free: a boundary is simply
// a texel whose neighbour carries a different slot.

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
layout(set = 1, binding = 3) uniform sampler2D uOwner;     // r = palette slot * (1/255)
layout(set = 1, binding = 4) uniform sampler2D uAtlas;     // shared sprite sheet
layout(set = 1, binding = 5) uniform sampler2D uRoads;     // r = road grade at full map resolution
layout(set = 1, binding = 6) uniform sampler2D uFog;       // r: 0 never seen, .5 seen once, 1 seen now
layout(set = 1, binding = 7) uniform Palette
{
    vec4 colors[128];   // slot 0 is "nothing here"
} palette;

layout(push_constant) uniform Push
{
    vec4 forestRect;   // xy = uv min, zw = uv max inside the atlas
    vec4 fieldRect;
    vec4 settings;     // x = forest tiling, y = field tiling, w = tint,
                       // z = border width; its SIGN says how the frontier is drawn -
                       // positive for the hard tile edge, negative for a softened one
    vec4 texel;        // xy = 1/ownerSize, zw = sun direction xy
    vec4 water;        // rgb = the palette colour of water, a = match tolerance
    vec4 flags;        // x = draw the thematic layer, y = edge strength,
                       // z = field opacity, w = density at which a field is fully painted
    vec4 roadColor;    // rgb = packed earth, a = opacity
    vec4 bridgeColor;  // rgb = timber decking, a = edge softness
    vec4 fog;          // x = on, y = seconds, z = dimming of explored land, w = cloud scale
    vec4 fogColor;     // rgb = the colour of the unknown, a = contrast of the clouds
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorld;

layout(location = 0) out vec4 outColor;

// --- the fog itself -------------------------------------------------------------------
// Slow rolling cloud, its own thing entirely: not a veil laid over the land but something
// drawn in place of it, so that unexplored country reads as absence rather than as dark.
float FogHash(vec2 p)
{
    p = fract(p * vec2(0.3183099, 0.3678794));
    p += dot(p, p + 27.31);
    return fract(p.x * p.y * 73.117);
}

float FogNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(FogHash(i),                FogHash(i + vec2(1.0, 0.0)), u.x),
               mix(FogHash(i + vec2(0.0, 1.0)), FogHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float FogFbm(vec2 p)
{
    float total = 0.0;
    float amplitude = 0.5;
    const mat2 turn = mat2(0.80, 0.60, -0.60, 0.80);
    for (int i = 0; i < 5; ++i)
    {
        total += FogNoise(p) * amplitude;
        p = turn * p * 2.07;
        amplitude *= 0.5;
    }
    return total;
}

vec3 FogColourAt(vec2 world, float time)
{
    const float k = pc.fog.w;

    // Two layers drifting at different speeds and angles: the bank never repeats and never
    // looks like a scrolling texture.
    vec2 drift = vec2(time * 0.009, time * -0.006);
    float low = FogFbm(world * k + drift);
    float high = FogFbm(world * k * 2.6 + drift * -1.7 + vec2(11.3, 4.1));

    float density = low * 0.65 + high * 0.35;
    density = mix(0.5, density, pc.fogColor.a * 2.0);

    vec3 base = pc.fogColor.rgb;
    return base * (0.72 + density * 0.75);
}

vec4 SampleTile(vec4 rect, vec2 uv, float tiling)
{
    vec2 local = fract(uv * tiling);
    return textureLod(uAtlas, mix(rect.xy, rect.zw, local), 0.0);
}

void main()
{
    // What the player knows about this spot decides everything that follows, so it is the
    // first thing asked. Unseen country is answered before the water discard: a river you
    // have never visited must be fog too, not a window onto the moving sea.
    float known = pc.fog.x > 0.5 ? textureLod(uFog, vUV, 0.0).r : 1.0;

    // How much of this spot is swallowed. The mask is already blurred on the way in, so a
    // wide smoothstep on top of it gives a front that rolls rather than snaps.
    float hidden = 1.0 - smoothstep(0.06, 0.46, known);
    if (hidden > 0.5)
    {
        outColor = vec4(FogColourAt(vWorld.xy, pc.fog.y), 1.0);
        return;
    }

    vec3 color = textureLod(uTerrain, vUV, 0.0).rgb;

    // Water is not drawn here at all: the sea plane underneath already carries the waves,
    // so sea and rivers move as one surface and the map has no hard edge.
    if (distance(color, pc.water.rgb) < pc.water.a) discard;

    // --- forest ------------------------------------------------------------------------
    // The mask is one texel per tile and is filtered smoothly, so a full wood used to bleed
    // half a tile past its own ground and paint trees over the beach beside it and over the
    // hillside above it. The nearest-texel reading of the same mask says which tile the
    // fragment is actually standing on, and that decides whether there is a wood here at
    // all; the smooth reading is kept only to shade the inside of the stand.
    vec2 treeTexels = vec2(textureSize(uTrees, 0));
    vec2 treeCentre = (floor(vUV * treeTexels) + 0.5) / treeTexels;
    float forestHere = textureLod(uTrees, treeCentre, 0.0).r;
    float forest = forestHere > 0.02 ? max(textureLod(uTrees, vUV, 0.0).r, forestHere * 0.5) : 0.0;
    if (forest > 0.02)
    {
        vec4 tile = SampleTile(pc.forestRect, vUV, pc.settings.x);
        color = mix(color, tile.rgb, tile.a * clamp(forest * 1.6, 0.0, 1.0));
    }

    // --- tilled fields ------------------------------------------------------------------
    // Ploughed land is ploughed land. Fading the furrows by the raw density made a mature
    // field read as a faint smudge, so the density only decides *where* the field reaches:
    // past the knee the pattern is painted at full strength, and only the ragged outer
    // edge of the worked land is left to fade into the grass.
    // Gated the same way as the wood above: ploughland stops at the edge of the tile that
    // is ploughed, instead of smearing furrows across the sand next door.
    vec2 fieldTexels = vec2(textureSize(uFields, 0));
    vec2 fieldCentre = (floor(vUV * fieldTexels) + 0.5) / fieldTexels;
    float fieldHere = textureLod(uFields, fieldCentre, 0.0).r;
    float field = fieldHere > 0.01 ? max(textureLod(uFields, vUV, 0.0).r, fieldHere * 0.5) : 0.0;
    if (field > 0.01)
    {
        vec4 tile = SampleTile(pc.fieldRect, vUV, pc.settings.y);
        float cover = smoothstep(0.0, max(pc.flags.w, 0.02), field);
        color = mix(color, tile.rgb, cover * tile.a * pc.flags.z);
    }

    // --- roads ---------------------------------------------------------------------------
    // The layer is rasterised at the map's own pixel resolution, so the edge here is worth
    // barely half a texel: a hard threshold with a sliver of smoothing keeps the track crisp
    // instead of the wide fuzzy band a tile-resolution mask would give.
    float road = textureLod(uRoads, vUV, 0.0).r;
    if (road > 0.02)
    {
        float soft = max(pc.bridgeColor.a, 0.01);
        float cover = smoothstep(0.25 - soft, 0.25 + soft, road);
        vec3 surface = mix(pc.roadColor.rgb, pc.bridgeColor.rgb, smoothstep(0.62, 0.85, road));
        color = mix(color, surface, cover * pc.roadColor.a);
    }

    // --- the thematic layer --------------------------------------------------------------
    float ownerRaw = textureLod(uOwner, vUV, 0.0).r;
    int ownerSlot = int(ownerRaw * 255.0 + 0.5);
    if (ownerSlot > 0 && pc.flags.x > 0.5)
    {
        vec4 realm = palette.colors[min(ownerSlot, 127)];

        float width = abs(pc.settings.z);
        bool soften = pc.settings.z < 0.0;

        if (!soften)
        {
            // The frontier as the grid actually has it: a texel whose neighbour carries a
            // different slot is on the line, and the line is therefore a tile wide and
            // steps like one. In realm mode that draws the outer border; in influence mode
            // the same test also draws the seams between one lord's holdings, because each
            // holding owns its own slot.
            color = mix(color, realm.rgb, pc.settings.w * realm.a);

            float edge = 0.0;
            for (int i = -1; i <= 1; ++i)
            {
                for (int j = -1; j <= 1; ++j)
                {
                    if (i == 0 && j == 0) continue;
                    float n = textureLod(uOwner, vUV + vec2(float(i), float(j)) * pc.texel.xy * width, 0.0).r;
                    edge = max(edge, abs(n - ownerRaw) > 0.001 ? 1.0 : 0.0);
                }
            }
            color = mix(color, realm.rgb, edge * pc.flags.y);
        }
        else
        {
            // The same question asked over a disc instead of over one ring: how much of the
            // neighbourhood answers to this slot. Deep inside a realm that is 1, on the line
            // it is about a half, and outside it falls away - so the tint fades out across
            // the frontier and the border itself becomes the band where the answer is
            // uncertain. Corners round off for free, because a corner has less of its
            // neighbourhood on the inside than a straight stretch does.
            const vec2 kTaps[16] = vec2[16](
                vec2( 1.0,  0.0), vec2(-1.0,  0.0), vec2( 0.0,  1.0), vec2( 0.0, -1.0),
                vec2( 0.7,  0.7), vec2(-0.7,  0.7), vec2( 0.7, -0.7), vec2(-0.7, -0.7),
                vec2( 2.0,  0.0), vec2(-2.0,  0.0), vec2( 0.0,  2.0), vec2( 0.0, -2.0),
                vec2( 1.5,  1.5), vec2(-1.5,  1.5), vec2( 1.5, -1.5), vec2(-1.5, -1.5));

            float inside = 1.4;          // the texel itself, weighted as the near ring
            float total = 1.4;
            for (int i = 0; i < 16; ++i)
            {
                float weight = i < 8 ? 1.0 : 0.5;
                vec2 uv = vUV + kTaps[i] * pc.texel.xy * width;
                float n = textureLod(uOwner, uv, 0.0).r;
                inside += weight * (abs(n - ownerRaw) < 0.001 ? 1.0 : 0.0);
                total += weight;
            }
            float cover = inside / total;

            // The fill holds over the interior and lets go near the line, over a long ramp
            // so the country bleeds into the frontier instead of stopping at it.
            color = mix(color, realm.rgb, pc.settings.w * realm.a * smoothstep(0.18, 0.78, cover));

            // ...and the line is drawn where the cover is passing through a half: a broad,
            // soft band with no edge of its own, whatever the tiles underneath are doing.
            // The falloff is deliberately gentle - a drawn border on an old map is a wash of
            // colour along the march, not a wire.
            float distance = (cover - 0.50) / 0.34;
            float edge = exp(-distance * distance);
            color = mix(color, realm.rgb, edge * pc.flags.y * 0.85);
        }
    }

    // --- lighting -----------------------------------------------------------------------
    vec3 sun = normalize(vec3(pc.texel.z, pc.texel.w, 0.85));
    float lambert = clamp(dot(normalize(vNormal), sun), 0.0, 1.0);
    float shade = 0.62 + 0.45 * lambert;
    color *= shade;

    // Land you have walked but are not watching keeps its shape and loses its light, and
    // fades towards the fog at the very edge of memory.
    if (pc.fog.x > 0.5)
    {
        // Two blends, one into the other: the further from anyone's eyes, the closer the
        // land gets to the fog it will become.
        float remembered = 1.0 - smoothstep(0.5, 0.97, known);
        vec3 bank = FogColourAt(vWorld.xy, pc.fog.y);
        color = mix(color, bank * 0.85, remembered * pc.fog.z);
        color = mix(color, bank, smoothstep(0.0, 0.5, hidden) * 0.9);
    }

    outColor = vec4(color, 1.0);
}
