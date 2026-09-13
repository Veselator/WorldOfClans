#include "MapGenerator.h"
#include "TerrainTypes.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"
#include "../../Core/Random.h"

#include <algorithm>
#include <cmath>

namespace woc
{
    namespace
    {
        /// Value noise with smooth interpolation; cheap, deterministic and good enough
        /// for a landscape that a designer will then paint over anyway.
        f32 Hash2D(i32 x, i32 y, u32 seed)
        {
            u32 h = static_cast<u32>(x) * 374761393u + static_cast<u32>(y) * 668265263u + seed * 2246822519u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<f32>((h ^ (h >> 16)) & 0xFFFFFF) / static_cast<f32>(0xFFFFFF);
        }

        f32 SmoothNoise(f32 x, f32 y, u32 seed)
        {
            const i32 x0 = static_cast<i32>(std::floor(x));
            const i32 y0 = static_cast<i32>(std::floor(y));
            const f32 fx = x - static_cast<f32>(x0);
            const f32 fy = y - static_cast<f32>(y0);

            const f32 ux = fx * fx * (3.0f - 2.0f * fx);
            const f32 uy = fy * fy * (3.0f - 2.0f * fy);

            const f32 a = Hash2D(x0, y0, seed);
            const f32 b = Hash2D(x0 + 1, y0, seed);
            const f32 c = Hash2D(x0, y0 + 1, seed);
            const f32 d = Hash2D(x0 + 1, y0 + 1, seed);

            return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
        }

        f32 Fbm(f32 x, f32 y, u32 seed, int octaves)
        {
            f32 total = 0.0f;
            f32 amplitude = 1.0f;
            f32 frequency = 1.0f;
            f32 normalisation = 0.0f;
            for (int i = 0; i < octaves; ++i)
            {
                total += SmoothNoise(x * frequency, y * frequency, seed + static_cast<u32>(i) * 71u) * amplitude;
                normalisation += amplitude;
                amplitude *= 0.5f;
                frequency *= 2.0f;
            }
            return normalisation > 0.0f ? total / normalisation : 0.0f;
        }

        /// Ridged noise: the fold of the ordinary kind. Where smooth fBm makes hills with
        /// summits scattered anywhere, this makes *lines* - the crest of each octave falls
        /// along the zero of the noise, so the octaves stack into chains with passes in
        /// them. It is the difference between one mountain and a range.
        f32 Ridged(f32 x, f32 y, u32 seed, int octaves)
        {
            f32 total = 0.0f;
            f32 amplitude = 1.0f;
            f32 frequency = 1.0f;
            f32 normalisation = 0.0f;
            f32 weight = 1.0f;

            for (int i = 0; i < octaves; ++i)
            {
                f32 value = SmoothNoise(x * frequency, y * frequency, seed + static_cast<u32>(i) * 137u);
                value = 1.0f - std::abs(value * 2.0f - 1.0f);
                value *= value;

                // Each octave is damped by the one above it, so detail gathers on the ridges
                // rather than being sprinkled evenly over the whole field.
                value *= weight;
                weight = Clamp01(value * 2.0f);

                total += value * amplitude;
                normalisation += amplitude;
                amplitude *= 0.55f;
                frequency *= 2.1f;
            }
            return normalisation > 0.0f ? total / normalisation : 0.0f;
        }

        /// Distance from a point to a segment, in the same units the points are in. Used to
        /// lay the land bridges between islands.
        f32 DistanceToSegment(const Vec2& point, const Vec2& a, const Vec2& b)
        {
            const Vec2 along{ b.x - a.x, b.y - a.y };
            const f32 lengthSq = along.x * along.x + along.y * along.y;
            if (lengthSq < 0.0001f) return Distance(point, a);

            const f32 t = Clamp01(((point.x - a.x) * along.x + (point.y - a.y) * along.y) / lengthSq);
            const Vec2 nearest{ a.x + along.x * t, a.y + along.y * t };
            return Distance(point, nearest);
        }
    }

    Json MapGenSettings::ToJson() const
    {
        Json node = Json::MakeObject();
        node["seed"] = static_cast<i64>(seed);
        node["width"] = static_cast<i64>(width);
        node["height"] = static_cast<i64>(height);
        node["tilePixels"] = static_cast<i64>(tilePixels);
        node["scale"] = scale;
        node["seaLevel"] = seaLevel;
        node["mountains"] = mountains;
        node["forest"] = forest;
        node["relief"] = relief;
        node["reliefCurve"] = reliefCurve;
        node["seaMargin"] = seaMargin;
        node["octaves"] = octaves;
        node["ridges"] = ridges;
        node["coastRoughness"] = coastRoughness;
        node["landMass"] = landMass;
        node["continent"] = continent;
        node["aridity"] = aridity;
        node["islands"] = islands;
        node["isthmusWidth"] = isthmusWidth;
        return node;
    }

    MapGenSettings MapGenSettings::FromJson(const Json& node)
    {
        MapGenSettings settings;
        settings.seed = static_cast<u32>(node["seed"].AsInt(0));
        settings.width = static_cast<u32>(node["width"].AsInt(1920));
        settings.height = static_cast<u32>(node["height"].AsInt(1080));
        settings.tilePixels = static_cast<u32>(node["tilePixels"].AsInt(4));
        settings.scale = node["scale"].AsFloat(3.2f);
        settings.seaLevel = node["seaLevel"].AsFloat(0.42f);
        settings.mountains = node["mountains"].AsFloat(0.78f);
        settings.forest = node["forest"].AsFloat(0.45f);
        settings.relief = node["relief"].AsFloat(0.95f);
        settings.reliefCurve = node["reliefCurve"].AsFloat(1.7f);
        settings.seaMargin = node["seaMargin"].AsFloat(0.09f);
        settings.octaves = node["octaves"].AsInt(5);
        settings.ridges = node["ridges"].AsFloat(0.4f);
        settings.coastRoughness = node["coastRoughness"].AsFloat(0.25f);
        settings.landMass = node["landMass"].AsFloat(0.55f);
        settings.continent = node["continent"].AsFloat(0.5f);
        settings.aridity = node["aridity"].AsFloat(0.0f);
        settings.islands = node["islands"].AsInt(0);
        settings.isthmusWidth = node["isthmusWidth"].AsFloat(26.0f);
        return settings;
    }

    // =========================================================================================
    // The worlds on offer
    // =========================================================================================

    const std::vector<MapGenPreset>& MapGenerator::Presets()
    {
        static const std::vector<MapGenPreset> kPresets = []
        {
            std::vector<MapGenPreset> presets;

            {
                MapGenPreset preset;
                preset.name = "Помірний край";
                preset.description = "Один великий острів із хребтом посередині, лісами й "
                                     "рівнинами обабіч. Звичайний світ, з якого добре починати.";
                preset.settings.scale = 3.2f;
                preset.settings.octaves = 5;
                preset.settings.ridges = 0.4f;
                preset.settings.coastRoughness = 0.22f;
                preset.settings.landMass = 0.55f;
                preset.settings.continent = 0.5f;
                preset.settings.forest = 0.45f;
                preset.settings.mountains = 0.78f;
                presets.push_back(preset);
            }
            {
                MapGenPreset preset;
                preset.name = "Порізане узбережжя";
                preset.description = "Затоки, миси й фіорди: берегова лінія довша за саму "
                                     "землю, а до сусіда часто ближче морем, ніж суходолом.";
                preset.settings.scale = 4.6f;
                preset.settings.octaves = 6;
                preset.settings.ridges = 0.5f;
                preset.settings.coastRoughness = 0.95f;
                preset.settings.landMass = 0.52f;
                preset.settings.continent = 0.2f;
                preset.settings.forest = 0.55f;
                preset.settings.mountains = 0.72f;
                preset.settings.seaLevel = 0.44f;
                presets.push_back(preset);
            }
            {
                MapGenPreset preset;
                preset.name = "Великий материк";
                preset.description = "Одна суцільна земля від краю до краю, з довгими "
                                     "хребтами й широкими рівнинами. Місця вистачить усім — "
                                     "і сваритися буде за що.";
                preset.settings.scale = 2.4f;
                preset.settings.octaves = 5;
                preset.settings.ridges = 0.55f;
                preset.settings.coastRoughness = 0.12f;
                preset.settings.landMass = 0.82f;
                preset.settings.continent = 1.0f;
                preset.settings.forest = 0.42f;
                preset.settings.mountains = 0.8f;
                preset.settings.seaLevel = 0.36f;
                presets.push_back(preset);
            }
            {
                MapGenPreset preset;
                preset.name = "Пустельний край";
                preset.description = "Випалена земля: суглинок і пісок, ріденькі гаї коло "
                                     "води, каміння в пагорбах. Голодний світ.";
                preset.settings.scale = 2.8f;
                preset.settings.octaves = 4;
                preset.settings.ridges = 0.45f;
                preset.settings.coastRoughness = 0.18f;
                preset.settings.landMass = 0.72f;
                preset.settings.continent = 0.75f;
                preset.settings.forest = 0.06f;
                preset.settings.aridity = 1.0f;
                preset.settings.mountains = 0.76f;
                preset.settings.seaLevel = 0.38f;
                presets.push_back(preset);
            }
            {
                MapGenPreset preset;
                preset.name = "Острівні держави";
                preset.description = "Кожній державі — свій острів, а між ними вузькі "
                                     "перешийки. Хто тримає перешийок, той тримає війну.";
                preset.settings.scale = 3.6f;
                preset.settings.octaves = 5;
                preset.settings.ridges = 0.35f;
                preset.settings.coastRoughness = 0.45f;
                preset.settings.landMass = 0.5f;
                preset.settings.continent = 0.0f;
                preset.settings.forest = 0.5f;
                preset.settings.mountains = 0.8f;
                preset.settings.seaLevel = 0.42f;
                // The party screen overwrites this with the realm count; two is the floor.
                preset.settings.islands = 5;
                preset.settings.isthmusWidth = 26.0f;
                presets.push_back(preset);
            }
            return presets;
        }();
        return kPresets;
    }

    // =========================================================================================
    // Building the thing
    // =========================================================================================

    u32 MapGenerator::Generate(MapData& map, MapDescription& description, const MapGenSettings& settings,
                               const std::string& name)
    {
        const u32 seed = settings.seed != 0
            ? settings.seed
            : static_cast<u32>(GlobalRandom().Range(1, 2000000000));

        const u32 width = std::max(256u, settings.width);
        const u32 height = std::max(256u, settings.height);
        const u32 tilePixels = std::max(1u, settings.tilePixels);
        map.Allocate(width, height, tilePixels);

        const TerrainDatabase& terrainDb = TerrainDatabase::Get();
        const u8 water = terrainDb.IndexOf("water");
        const u8 coast = terrainDb.IndexOf("coast");
        const u8 plainRich = terrainDb.IndexOf("plainRich");
        const u8 plain = terrainDb.IndexOf("plain");
        const u8 plainPoor = terrainDb.IndexOf("plainPoor");
        const u8 hills = terrainDb.IndexOf("hills");
        const u8 highland = terrainDb.IndexOf("highland");
        const u8 mountain = terrainDb.IndexOf("mountain");

        const f32 tilesX = static_cast<f32>(map.TileWidth());
        const f32 tilesY = static_cast<f32>(map.TileHeight());

        const i32 octaves = std::clamp(settings.octaves, 2, 8);
        const f32 ridgeShare = Clamp01(settings.ridges);
        const f32 roughness = std::max(0.0f, settings.coastRoughness);
        const f32 aridity = Clamp01(settings.aridity);

        // --- an archipelago of realms, when that is what was asked for ------------------------
        // Each island gets a centre and a radius; the isthmuses are the segments of the ring
        // joining them, so every island has two neighbours and the whole thing is one
        // connected country that an army can walk across without a single bridge.
        std::vector<Vec2> islandCentres;
        std::vector<f32> islandRadii;
        if (settings.islands >= 2)
        {
            Random layout(seed ^ 0x1513ADu);
            const i32 count = std::min(settings.islands, 12);

            // Laid out on an ellipse, jittered, so they are spread over the map rather than
            // clustered, and so the ring of isthmuses does not double back on itself.
            const f32 spreadX = 0.34f;
            const f32 spreadY = 0.34f;
            const f32 radius = std::min(static_cast<f32>(width), static_cast<f32>(height)) /
                               (1.6f + static_cast<f32>(count) * 0.22f);

            for (i32 i = 0; i < count; ++i)
            {
                const f32 angle = static_cast<f32>(i) / static_cast<f32>(count) * 2.0f * kPi;
                const f32 jitter = layout.RangeF(0.86f, 1.14f);
                islandCentres.push_back({
                    width * (0.5f + std::cos(angle) * spreadX * jitter),
                    height * (0.5f + std::sin(angle) * spreadY * jitter)
                });
                islandRadii.push_back(radius * layout.RangeF(0.85f, 1.2f));
            }
        }

        for (u32 ty = 0; ty < map.TileHeight(); ++ty)
        {
            for (u32 tx = 0; tx < map.TileWidth(); ++tx)
            {
                const f32 u = static_cast<f32>(tx) / tilesX;
                const f32 v = static_cast<f32>(ty) / tilesY;

                // --- the coast is dragged about before anything else is measured -----------
                // Warping the *input* to the falloff rather than the output is what gives
                // headlands and bays instead of a fuzzy circle: the shape itself moves.
                f32 wu = u;
                f32 wv = v;
                if (roughness > 0.001f)
                {
                    const f32 warpX = Fbm(u * settings.scale * 1.3f + 3.1f,
                                          v * settings.scale * 1.3f + 8.7f, seed + 5501u, 4) - 0.5f;
                    const f32 warpY = Fbm(u * settings.scale * 1.3f + 19.4f,
                                          v * settings.scale * 1.3f + 2.2f, seed + 7717u, 4) - 0.5f;
                    wu += warpX * roughness * 0.35f;
                    wv += warpY * roughness * 0.35f;
                }

                // --- where there is land at all ---------------------------------------------
                f32 mass = 0.0f;
                if (!islandCentres.empty())
                {
                    // One dome per realm, and a ridge of land along the ring joining them.
                    const Vec2 here{ wu * width, wv * height };
                    for (size_t i = 0; i < islandCentres.size(); ++i)
                    {
                        const f32 reach = 1.0f - Clamp01(Distance(here, islandCentres[i]) / islandRadii[i]);
                        mass = std::max(mass, reach);
                    }

                    if (settings.isthmusWidth > 0.5f)
                    {
                        for (size_t i = 0; i < islandCentres.size(); ++i)
                        {
                            const Vec2& a = islandCentres[i];
                            const Vec2& b = islandCentres[(i + 1) % islandCentres.size()];
                            const f32 distance = DistanceToSegment(here, a, b);
                            const f32 bridge = 1.0f - Clamp01(distance / settings.isthmusWidth);
                            // Just above the waterline and no more: a causeway, not a plain.
                            mass = std::max(mass, bridge * 0.62f);
                        }
                    }
                }
                else
                {
                    // A radial falloff, flattened towards a single broad dome as `continent`
                    // rises. At nought it is a sharp bowl and the noise breaks the land into
                    // islands; at one it barely bites except near the very edge.
                    const f32 dx = (wu - 0.5f) * 2.0f;
                    const f32 dy = (wv - 0.5f) * 2.0f;
                    const f32 distance = std::min(1.0f, std::sqrt(dx * dx * 0.75f + dy * dy * 1.15f));
                    const f32 shape = Lerp(2.1f, 0.75f, Clamp01(settings.continent));
                    mass = 1.0f - std::pow(distance, shape);
                }

                // --- and how high it stands ---------------------------------------------------
                const f32 smooth = Fbm(u * settings.scale, v * settings.scale, seed, octaves);
                const f32 chain = Ridged(u * settings.scale * 0.8f + 4.3f,
                                         v * settings.scale * 0.8f + 1.9f, seed + 3301u, octaves);
                const f32 relief = Lerp(smooth, chain, ridgeShare);

                f32 elevation = relief * 0.55f + mass * (0.35f + Clamp01(settings.landMass) * 0.55f);

                // ...and a band of open sea all the way round, so the world never ends in a
                // wall. The falloff alone leaves land against the middle of an edge, where
                // its own distance term is at its weakest.
                const f32 margin = std::max(0.01f, settings.seaMargin);
                const f32 rim = Clamp01(std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v)) / margin);
                elevation *= rim * rim * (3.0f - 2.0f * rim);

                elevation = Clamp01(elevation);

                // Moisture runs on its own field, and a dry world simply has less of it.
                f32 moisture = Fbm(u * settings.scale * 1.7f + 11.0f, v * settings.scale * 1.7f + 7.0f,
                                   seed + 991u, 4);
                moisture = Clamp01(moisture * (1.0f - aridity * 0.75f));

                // Height is measured up from the waterline, which is what a painted height
                // map means by it: the sea is nothing, the shore is barely anything, and the
                // ground climbs inland.
                const f32 aboveWater = Clamp01((elevation - settings.seaLevel) /
                                               std::max(0.05f, 1.0f - settings.seaLevel));

                Tile& tile = map.At({ static_cast<i32>(tx), static_cast<i32>(ty) });
                // Curved rather than straight, so the lowlands stay low and the climb is
                // saved for the last stretch. That is what lets the interior stand tall
                // without the shore turning into a step.
                tile.height = std::pow(aboveWater, settings.reliefCurve) * settings.relief;
                tile.field = 0.0f;

                if (elevation < settings.seaLevel)
                {
                    tile.terrain = water;
                    tile.height = 0.0f;
                    tile.forest = 0.0f;
                }
                else if (elevation < settings.seaLevel + 0.035f)
                {
                    // A beach is a beach: level with the water, not a step above it.
                    tile.terrain = coast;
                    tile.height = 0.0f;
                    tile.forest = 0.0f;
                }
                else if (elevation > settings.mountains + 0.13f)
                {
                    tile.terrain = mountain;
                    tile.forest = 0.0f;
                }
                else if (elevation > settings.mountains)
                {
                    tile.terrain = highland;
                    tile.forest = 0.0f;
                }
                else if (elevation > settings.mountains - 0.12f)
                {
                    tile.terrain = hills;
                    tile.forest = 0.0f;
                }
                else
                {
                    // Soil quality follows moisture: the wettest lowlands are the richest.
                    // In a dry world the black earth never appears at all and the driest
                    // ground goes to sand, which is what a desert reads as on this palette.
                    if (aridity > 0.6f && moisture < 0.22f) tile.terrain = coast;
                    else if (moisture > Lerp(0.62f, 0.86f, aridity)) tile.terrain = plainRich;
                    else if (moisture > Lerp(0.40f, 0.62f, aridity)) tile.terrain = plain;
                    else tile.terrain = plainPoor;

                    // Trees only stand down here - the plains are the only ground a wood
                    // takes, in the generator exactly as in the living world - and they
                    // follow the water, so a dry map keeps its groves near the rivers.
                    const f32 forestNoise = Fbm(u * settings.scale * 2.6f + 31.0f,
                                                v * settings.scale * 2.6f + 17.0f, seed + 4211u, 4);
                    const f32 appetite = settings.forest * (0.35f + moisture * 0.9f);
                    tile.forest = forestNoise > (1.0f - appetite)
                        ? Clamp01((forestNoise - (1.0f - appetite)) * 3.0f)
                        : 0.0f;
                    if (tile.terrain == coast) tile.forest = 0.0f;
                }
            }
        }

        map.RebuildElevation(ConfigManager::Get().Int("render/terrainSmoothPasses", 4));
        map.ComputeFordableWater(ConfigManager::Get().Int("map/fordableWaterRadius", 3));
        RebuildColorLayer(map);

        description.name = name.empty() ? "Новий світ" : name;
        description.width = width;
        description.height = height;
        description.tilePixels = tilePixels;
        description.seed = seed;
        return seed;
    }

    void MapGenerator::RebuildColorLayer(MapData& map)
    {
        std::vector<u8>& pixels = map.ColorPixels();
        pixels.assign(static_cast<size_t>(map.PixelWidth()) * map.PixelHeight() * 4, 255);

        for (u32 y = 0; y < map.PixelHeight(); ++y)
        {
            for (u32 x = 0; x < map.PixelWidth(); ++x)
            {
                const Coord tile = map.ToTile({ static_cast<f32>(x), static_cast<f32>(y) });
                const u32 color = TerrainDatabase::Get().At(map.At(tile).terrain).color;
                u8* texel = pixels.data() + (static_cast<size_t>(y) * map.PixelWidth() + x) * 4;
                texel[0] = static_cast<u8>((color >> 16) & 0xFF);
                texel[1] = static_cast<u8>((color >> 8) & 0xFF);
                texel[2] = static_cast<u8>(color & 0xFF);
                texel[3] = 255;
            }
        }
    }

    MapGenSettings& MapGenerator::Shared()
    {
        // Lives for as long as the program does and belongs to nobody in particular, which
        // is exactly what "the settings the player last chose" is.
        static MapGenSettings settings;
        return settings;
    }

    std::string MapGenerator::GenerateAndStore(const MapGenSettings& settings, MapDescription& outDescription)
    {
        MapData map;
        const u32 seed = Generate(map, outDescription, settings);

        // Once it is on disk it is a map like any other: it can be listed, loaded from a
        // save, opened in the editor and handed to another player. Nothing downstream has
        // to know it was made a minute ago.
        const std::string folder = "Generated_" + std::to_string(seed);
        outDescription.folder = folder;
        outDescription.name = "Згенерований світ " + std::to_string(seed);
        outDescription.description = "Створено генератором: " +
            std::to_string(outDescription.width) + "x" + std::to_string(outDescription.height);

        if (!MapLoader::Save(folder, map, outDescription))
        {
            WOC_LOG_ERROR("Could not write the generated map to Maps/", folder);
            return std::string();
        }

        MapLoader::SaveMinimap(folder, map);
        WOC_LOG_INFO("Generated map stored as Maps/", folder);
        return folder;
    }
}
