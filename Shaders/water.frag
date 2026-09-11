#version 450

// Procedural sea.
//
// Regularity is the enemy here: a couple of sine waves read as corrugated iron. So the
// surface is built from long swells travelling in directions that never line up, their
// frequencies deliberately irrational relative to one another, over a domain that is
// itself warped by low-frequency noise. Nothing in it repeats on any scale the player
// can see.

layout(set = 0, binding = 0) uniform Globals
{
    mat4 view;
    mat4 proj;
    mat4 uiProj;
    vec4 params;      // x = seconds
} g;

layout(push_constant) uniform Push
{
    vec4 deepColor;
    vec4 shallowColor;
    vec4 settings;    // x = base wave number, y = wave speed, z = glint strength, w = chop
    vec4 bounds;      // xy = map size in map units
    vec4 fog;         // x = on, y = seconds, z = cloud scale, w = contrast
    vec4 fogColor;    // rgb = the colour of the unknown
    vec4 foam;        // x = strength, y = width of the band, z = lace scale, w = churn
} pc;

layout(set = 1, binding = 0) uniform sampler2D uShore;   // r = nearness to the waterline

layout(location = 0) in vec2 vWorld;

layout(location = 0) out vec4 outColor;

// --- value noise ---------------------------------------------------------------------
float Hash(vec2 p)
{
    p = fract(p * vec2(0.3183099, 0.3678794));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y * 95.4337);
}

float Noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(mix(Hash(i),               Hash(i + vec2(1.0, 0.0)), u.x),
               mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float Fbm(vec2 p)
{
    float total = 0.0;
    float amplitude = 0.5;
    // Rotate between octaves so the lattice of the noise never shows through.
    const mat2 turn = mat2(0.80, 0.60, -0.60, 0.80);
    for (int i = 0; i < 4; ++i)
    {
        total += Noise(p) * amplitude;
        p = turn * p * 2.03;
        amplitude *= 0.5;
    }
    return total;
}

// One travelling swell. `dir` need not be normalised to anything tidy - that is the point.
float Swell(vec2 p, vec2 dir, float waveNumber, float speed, float time)
{
    return sin(dot(p, dir) * waveNumber - time * speed);
}

// The same bank of cloud the land is hidden under - and it has to be *exactly* the same,
// down to the hash, or the edge of the terrain mesh shows up as a seam drawn across the
// fog. These three functions are the ones in terrain.frag, letter for letter.
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
    return mix(mix(FogHash(i),                 FogHash(i + vec2(1.0, 0.0)), u.x),
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
    vec2 drift = vec2(time * 0.009, time * -0.006);
    float low = FogFbm(world * pc.fog.z + drift);
    float high = FogFbm(world * pc.fog.z * 2.6 + drift * -1.7 + vec2(11.3, 4.1));

    float density = mix(0.5, low * 0.65 + high * 0.35, pc.fog.w * 2.0);
    return pc.fogColor.rgb * (0.72 + density * 0.75);
}

void main()
{
    const float time = g.params.x;

    // The plane reaches far past the map on every side. With fog on, everything out there
    // is territory nobody has ever crossed, and it is drawn as such.
    if (pc.fog.x > 0.5)
    {
        // vWorld carries renderer world space, where y runs opposite to the map's.
        // A hair inside the boundary rather than exactly on it: the terrain mesh meets the
        // edge to the unit, and without the overlap the two surfaces rasterise to a seam.
        const float inset = 1.0;
        vec2 onMap = vec2(vWorld.x, -vWorld.y);
        if (onMap.x < inset || onMap.y < inset ||
            onMap.x > pc.bounds.x - inset || onMap.y > pc.bounds.y - inset)
        {
            outColor = vec4(FogColourAt(vWorld, time), 1.0);
            return;
        }
    }
    const float k = pc.settings.x;          // base wave number: smaller = longer waves
    const float speed = pc.settings.y;

    // Warp the domain first: the swells then bend and wander instead of marching in ranks.
    vec2 warp = vec2(Fbm(vWorld * k * 0.55 + vec2(0.0, time * 0.04)),
                     Fbm(vWorld * k * 0.55 + vec2(5.2, 1.3 - time * 0.03)));
    vec2 p = vWorld + (warp - 0.5) * (2.6 / k);

    // Five long swells. The directions are mutually irrational and the wave numbers are
    // spaced by non-integer ratios, so the sum has no short period.
    float height =
          Swell(p, vec2( 0.9239,  0.3827), k * 1.00, speed * 1.00, time) * 0.34
        + Swell(p, vec2(-0.2588,  0.9659), k * 1.37, speed * 0.86, time) * 0.26
        + Swell(p, vec2( 0.5878, -0.8090), k * 1.91, speed * 1.24, time) * 0.19
        + Swell(p, vec2(-0.9511, -0.3090), k * 2.71, speed * 0.71, time) * 0.13
        + Swell(p, vec2( 0.1045,  0.9945), k * 3.83, speed * 1.53, time) * 0.08;

    // A slow, large-scale swell field: some stretches of sea are simply calmer than others.
    float calm = 0.55 + 0.45 * Fbm(vWorld * k * 0.13 + vec2(time * 0.02, 0.0));
    height *= calm;

    // Fine chop, carried by the noise rather than by another sine.
    float chop = Fbm(p * k * 6.0 + vec2(time * 0.55, -time * 0.42)) - 0.5;
    height += chop * pc.settings.w;

    vec3 color = mix(pc.deepColor.rgb, pc.shallowColor.rgb, clamp(height * 0.9 + 0.5, 0.0, 1.0));

    // --- surf ---------------------------------------------------------------------------
    // Where the sea runs out of depth it breaks. The CPU hands over how near each stretch of
    // water is to the waterline; the shader turns that into a band, eats ragged holes in it
    // with noise, and lets the swell underneath push it in and out, so the white crawls along
    // the coast instead of sitting there like a drawn outline. The land side of the same band
    // is drawn by the terrain pass from this very mask, and the two meet at the waterline.
    vec2 shoreUV = vec2(vWorld.x, -vWorld.y) / max(pc.bounds.xy, vec2(1.0));
    if (pc.foam.x > 0.001 &&
        shoreUV.x > 0.0 && shoreUV.y > 0.0 && shoreUV.x < 1.0 && shoreUV.y < 1.0)
    {
        float shore = textureLod(uShore, shoreUV, 0.0).r;
        if (shore > 0.001)
        {
            // Two independent slow fields over the coast: one says how far the surf runs
            // up, the other how heavy it is. A single one would only stretch the same band.
            float temper = Fbm(vWorld * pc.foam.z * 0.22 + vec2(3.7, -1.9));
            float mood   = Fbm(vWorld * pc.foam.z * 0.13 + vec2(-8.1, 5.4));
            float width = clamp(pc.foam.y, 0.05, 1.0) * (0.40 + 1.45 * temper);
            float heavy = smoothstep(0.28, 0.78, mood);
            float band = smoothstep(1.0 - clamp(width, 0.05, 1.0), 1.0, shore);

            // Two scales of lace: coarse gaps where the surf is thin, fine froth inside it.
            float lace = Fbm(vWorld * pc.foam.z + vec2(time * 0.12, -time * 0.09));
            float froth = Fbm(vWorld * pc.foam.z * 3.1 + vec2(-time * 0.31, time * 0.26));

            // The swell decides when the water is actually running up the beach.
            float surge = 0.5 + 0.5 * sin(height * 3.4 - time * 0.9);
            float wash = band * (0.45 + 0.85 * lace) * (0.55 + 0.65 * surge * pc.foam.w);

            float foam = smoothstep(0.24, 0.62, wash) * (0.55 + 0.75 * froth);
            foam *= 0.25 + 1.0 * heavy;
            color = mix(color, vec3(1.0, 1.0, 0.985), clamp(foam, 0.0, 1.0) * pc.foam.x);
        }
    }

    // Glints ride the steepest part of the crests, and only where the sea is lively.
    float crest = smoothstep(0.42, 0.86, height) * calm;
    color += vec3(1.0, 1.0, 0.96) * crest * pc.settings.z;

    // Depth-ish shading: the troughs read a little darker and greener.
    color *= 0.94 + 0.10 * smoothstep(-0.6, 0.6, height);

    outColor = vec4(color, 1.0);
}
