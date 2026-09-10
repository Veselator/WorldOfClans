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
} pc;

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

void main()
{
    const float time = g.params.x;
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

    // Glints ride the steepest part of the crests, and only where the sea is lively.
    float crest = smoothstep(0.42, 0.86, height) * calm;
    color += vec3(1.0, 1.0, 0.96) * crest * pc.settings.z;

    // Depth-ish shading: the troughs read a little darker and greener.
    color *= 0.94 + 0.10 * smoothstep(-0.6, 0.6, height);

    outColor = vec4(color, 1.0);
}
