#include "MapLoader.h"
#include "../../Core/Config.h"
#include "../../Core/ImageIO.h"
#include "../../Core/Json.h"
#include "../../Core/Log.h"
#include "../../Core/Paths.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        constexpr const char* kMapFile = "Map.json";

        Json ReadMapJson(const std::string& folder)
        {
            return Json::LoadFile(Paths::Get().Map(folder, kMapFile));
        }
    }

    bool MapLoader::ReadDescription(const std::string& folder, MapDescription& out)
    {
        const Json doc = ReadMapJson(folder);
        if (doc.IsNull()) return false;

        out.folder = folder;
        out.name = doc.GetString("name", folder);
        out.description = doc.GetString("description");
        out.width = static_cast<u32>(doc.GetInt("size/width", 0));
        out.height = static_cast<u32>(doc.GetInt("size/height", 0));
        out.tilePixels = static_cast<u32>(doc.GetInt("tilePixels", 4));
        out.seed = static_cast<u32>(doc.GetInt("seed", 0));
        return true;
    }

    std::vector<MapDescription> MapLoader::ListMaps()
    {
        std::vector<MapDescription> maps;
        for (const std::string& folder : Paths::ListDirectories(Paths::Get().MapsDirectory()))
        {
            MapDescription description;
            if (ReadDescription(folder, description)) maps.push_back(std::move(description));
        }
        std::sort(maps.begin(), maps.end(),
            [](const MapDescription& a, const MapDescription& b) { return a.name < b.name; });
        return maps;
    }

    bool MapLoader::Load(const std::string& folder, MapData& outMap, MapDescription& outDescription)
    {
        const Json doc = ReadMapJson(folder);
        if (doc.IsNull())
        {
            WOC_LOG_ERROR("Map.json missing for map '", folder, "'");
            return false;
        }
        ReadDescription(folder, outDescription);

        const std::string terrainFile = doc.GetString("layers/terrain", "Terrain.png");
        const std::string heightFile = doc.GetString("layers/height", "HeightMap.png");
        const std::string treesFile = doc.GetString("layers/trees", "Trees.png");

        ImageData terrainImage;
        if (!LoadImage(Paths::Get().Map(folder, terrainFile), terrainImage, 4)) return false;

        ImageData heightImage;
        const bool hasHeight = LoadImage(Paths::Get().Map(folder, heightFile), heightImage, 1);

        ImageData treeImage;
        const bool hasTrees = LoadImage(Paths::Get().Map(folder, treesFile), treeImage, 4);

        const u32 tilePixels = std::max(1u, outDescription.tilePixels);
        outMap.Allocate(terrainImage.width, terrainImage.height, tilePixels);
        outDescription.width = terrainImage.width;
        outDescription.height = terrainImage.height;

        // The colour layer goes to the GPU untouched; the simulation reads the coarse grid.
        outMap.ColorPixels() = terrainImage.pixels;

        TerrainDatabase& terrainDb = TerrainDatabase::Get();

        // Cache colour lookups: a 1920x1080 map has millions of pixels but a handful of colours.
        std::vector<std::pair<u32, u8>> colorCache;
        auto matchCached = [&](u32 rgb) -> u8
        {
            for (const auto& [key, value] : colorCache) if (key == rgb) return value;
            const u8 index = terrainDb.MatchColor(rgb);
            colorCache.emplace_back(rgb, index);
            return index;
        };

        const u32 tilesX = outMap.TileWidth();
        const u32 tilesY = outMap.TileHeight();

        for (u32 ty = 0; ty < tilesY; ++ty)
        {
            for (u32 tx = 0; tx < tilesX; ++tx)
            {
                // Each tile takes the majority terrain of the pixels it covers, so a thin
                // river painted one pixel wide still survives the downsample.
                std::vector<u32> votes(terrainDb.Count(), 0);
                f32 heightSum = 0.0f;
                f32 treeSum = 0.0f;
                u32 samples = 0;

                for (u32 py = 0; py < tilePixels; ++py)
                {
                    for (u32 px = 0; px < tilePixels; ++px)
                    {
                        const u32 x = tx * tilePixels + px;
                        const u32 y = ty * tilePixels + py;
                        if (x >= terrainImage.width || y >= terrainImage.height) continue;

                        const u8* texel = terrainImage.At(x, y);
                        const u32 rgb = (static_cast<u32>(texel[0]) << 16) |
                                        (static_cast<u32>(texel[1]) << 8) |
                                         static_cast<u32>(texel[2]);
                        votes[matchCached(rgb)] += 1;

                        if (hasHeight && x < heightImage.width && y < heightImage.height)
                            heightSum += heightImage.At(x, y)[0] / 255.0f;

                        if (hasTrees && x < treeImage.width && y < treeImage.height)
                            treeSum += treeImage.At(x, y)[3] / 255.0f;

                        ++samples;
                    }
                }
                if (samples == 0) continue;

                u8 dominant = 0;
                u32 bestVotes = 0;
                for (size_t i = 0; i < votes.size(); ++i)
                {
                    if (votes[i] > bestVotes) { bestVotes = votes[i]; dominant = static_cast<u8>(i); }
                }

                // Rivers are narrow: if any pixel in the tile is water, keep the tile wet.
                const u8 waterIndex = terrainDb.IndexOf("water");
                if (votes[waterIndex] * 3 >= samples) dominant = waterIndex;

                Tile& tile = outMap.At({ static_cast<i32>(tx), static_cast<i32>(ty) });
                tile.terrain = dominant;
                tile.height = heightSum / static_cast<f32>(samples);
                tile.forest = Clamp01(treeSum / static_cast<f32>(samples));
                tile.field = 0.0f;
            }
        }

        outMap.RebuildElevation(ConfigManager::Get().Int("render/terrainSmoothPasses", 4));
        outMap.ComputeFordableWater(ConfigManager::Get().Int("map/fordableWaterRadius", 3));

        WOC_LOG_INFO("Map '", outDescription.name, "' loaded: ", terrainImage.width, "x", terrainImage.height,
                     " pixels, ", tilesX, "x", tilesY, " tiles");
        return true;
    }

    TerrainMesh MapLoader::BuildMesh(const MapData& map, u32 step)
    {
        TerrainMesh mesh;
        if (!map.IsValid()) return mesh;

        step = std::max(1u, step);
        const u32 columns = map.PixelWidth() / step + 1;
        const u32 rows = map.PixelHeight() / step + 1;

        mesh.vertices.resize(static_cast<size_t>(columns) * rows);

        auto heightAt = [&map](f32 mapX, f32 mapY)
        {
            return map.WorldHeightAtMap({ mapX, mapY });
        };

        for (u32 row = 0; row < rows; ++row)
        {
            for (u32 column = 0; column < columns; ++column)
            {
                const f32 mapX = static_cast<f32>(std::min(column * step, map.PixelWidth() - 1));
                const f32 mapY = static_cast<f32>(std::min(row * step, map.PixelHeight() - 1));
                const f32 height = heightAt(mapX, mapY);

                TerrainVertex& vertex = mesh.vertices[static_cast<size_t>(row) * columns + column];
                vertex.position = Camera::ToWorld({ mapX, mapY }, height);
                vertex.uv = { mapX / static_cast<f32>(map.PixelWidth()),
                              mapY / static_cast<f32>(map.PixelHeight()) };

                // Central differences give a smooth normal without storing a second pass.
                const f32 s = static_cast<f32>(step);
                const f32 hx = heightAt(std::min(mapX + s, static_cast<f32>(map.PixelWidth() - 1)), mapY) -
                               heightAt(std::max(mapX - s, 0.0f), mapY);
                const f32 hy = heightAt(mapX, std::min(mapY + s, static_cast<f32>(map.PixelHeight() - 1))) -
                               heightAt(mapX, std::max(mapY - s, 0.0f));
                vertex.normal = Vec3{ -hx, hy, 2.0f * s }.Normalized();
            }
        }

        mesh.indices.reserve(static_cast<size_t>(columns - 1) * (rows - 1) * 6);
        for (u32 row = 0; row + 1 < rows; ++row)
        {
            for (u32 column = 0; column + 1 < columns; ++column)
            {
                const u32 topLeft = row * columns + column;
                const u32 topRight = topLeft + 1;
                const u32 bottomLeft = topLeft + columns;
                const u32 bottomRight = bottomLeft + 1;

                mesh.indices.push_back(topLeft);
                mesh.indices.push_back(bottomLeft);
                mesh.indices.push_back(topRight);
                mesh.indices.push_back(topRight);
                mesh.indices.push_back(bottomLeft);
                mesh.indices.push_back(bottomRight);
            }
        }
        return mesh;
    }

    bool MapLoader::Save(const std::string& folder, const MapData& map, const MapDescription& description)
    {
        const std::string directory = Paths::Get().MapsDirectory() + "/" + folder;
        if (!Paths::EnsureDirectory(directory)) return false;

        Json doc = Json::MakeObject();
        doc["name"] = description.name;
        doc["description"] = description.description;
        doc["size"]["width"] = static_cast<i64>(map.PixelWidth());
        doc["size"]["height"] = static_cast<i64>(map.PixelHeight());
        doc["tilePixels"] = static_cast<i64>(map.TilePixels());
        doc["seed"] = static_cast<i64>(description.seed);
        doc["layers"]["terrain"] = "Terrain.png";
        doc["layers"]["height"] = "HeightMap.png";
        doc["layers"]["trees"] = "Trees.png";
        if (!doc.SaveFile(directory + "/" + kMapFile)) return false;

        // --- terrain colours ---------------------------------------------------------------
        ImageData terrainImage;
        terrainImage.width = map.PixelWidth();
        terrainImage.height = map.PixelHeight();
        terrainImage.channels = 4;
        terrainImage.pixels = map.ColorPixels();
        if (terrainImage.pixels.empty())
        {
            terrainImage.pixels.assign(static_cast<size_t>(terrainImage.width) * terrainImage.height * 4, 0);
            for (u32 y = 0; y < terrainImage.height; ++y)
            {
                for (u32 x = 0; x < terrainImage.width; ++x)
                {
                    const TerrainInfo& info = map.TerrainAtMap({ static_cast<f32>(x), static_cast<f32>(y) });
                    u8* texel = terrainImage.At(x, y);
                    texel[0] = static_cast<u8>((info.color >> 16) & 0xFF);
                    texel[1] = static_cast<u8>((info.color >> 8) & 0xFF);
                    texel[2] = static_cast<u8>(info.color & 0xFF);
                    texel[3] = 255;
                }
            }
        }
        if (!SaveImagePNG(directory + "/Terrain.png", terrainImage)) return false;

        // --- height and trees at tile resolution, upscaled to map resolution ------------------
        ImageData heightImage;
        heightImage.width = map.PixelWidth();
        heightImage.height = map.PixelHeight();
        heightImage.channels = 1;
        heightImage.pixels.assign(static_cast<size_t>(heightImage.width) * heightImage.height, 0);

        ImageData treeImage;
        treeImage.width = map.PixelWidth();
        treeImage.height = map.PixelHeight();
        treeImage.channels = 4;
        treeImage.pixels.assign(static_cast<size_t>(treeImage.width) * treeImage.height * 4, 0);

        for (u32 y = 0; y < heightImage.height; ++y)
        {
            for (u32 x = 0; x < heightImage.width; ++x)
            {
                const Tile& tile = map.At(map.ToTile({ static_cast<f32>(x), static_cast<f32>(y) }));
                heightImage.At(x, y)[0] = static_cast<u8>(Clamp01(tile.height) * 255.0f + 0.5f);
                u8* texel = treeImage.At(x, y);
                texel[0] = 40; texel[1] = 90; texel[2] = 40;
                texel[3] = static_cast<u8>(Clamp01(tile.forest) * 255.0f + 0.5f);
            }
        }

        return SaveImagePNG(directory + "/HeightMap.png", heightImage) &&
               SaveImagePNG(directory + "/Trees.png", treeImage);
    }
}
