#include "TerrainTypes.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <cstdlib>

namespace woc
{
    namespace
    {
        u32 ParseHexColor(const std::string& text)
        {
            return static_cast<u32>(std::strtoul(text.c_str(), nullptr, 16));
        }
    }

    void TerrainDatabase::Load()
    {
        m_types.clear();

        const Json& doc = ConfigManager::Get().Terrain();
        m_heightScale = doc.GetFloat("heightScale", 42.0f);
        m_fordableMoveCost = doc.GetFloat("fordableWater/moveCost", 4.5f);
        m_fordableCoverageCost = doc.GetFloat("fordableWater/coverageCost", 3.5f);

        for (const Json& entry : doc["types"].AsArray())
        {
            TerrainInfo info;
            info.id = entry["id"].AsString();
            info.name = entry["name"].AsString(info.id);
            info.color = ParseHexColor(entry["color"].AsString("ffffff"));
            info.passable = entry.Has("passable") ? entry["passable"].AsBool(true) : true;
            info.water = entry["water"].AsBool(false);
            info.buildable = entry.Has("buildable") ? entry["buildable"].AsBool(true) : true;
            info.moveCost = entry["moveCost"].AsFloat(1.0f);
            info.coverageCost = entry["coverageCost"].AsFloat(1.0f);
            info.soil = entry["soil"].AsFloat(1.0f);
            info.defense = entry["defense"].AsFloat(1.0f);
            info.stone = entry["stone"].AsFloat(0.0f);
            info.heightFactor = entry["heightFactor"].AsFloat(0.5f);
            m_types.push_back(std::move(info));
        }

        if (m_types.empty())
        {
            WOC_LOG_ERROR("terrain.json defined no terrain types; falling back to a single plain");
            TerrainInfo fallback;
            fallback.id = "plain";
            fallback.name = "Рівнина";
            fallback.color = 0x00FF00;
            m_types.push_back(fallback);
        }

        WOC_LOG_INFO("Terrain database: ", m_types.size(), " types");
    }

    const TerrainInfo& TerrainDatabase::At(u8 index) const
    {
        if (index < m_types.size()) return m_types[index];
        return m_types.front();
    }

    u8 TerrainDatabase::MatchColor(u32 rgb) const
    {
        const i32 r = static_cast<i32>((rgb >> 16) & 0xFF);
        const i32 g = static_cast<i32>((rgb >> 8) & 0xFF);
        const i32 b = static_cast<i32>(rgb & 0xFF);

        u8 best = 0;
        i32 bestDistance = INT32_MAX;
        for (size_t i = 0; i < m_types.size(); ++i)
        {
            const u32 c = m_types[i].color;
            const i32 dr = r - static_cast<i32>((c >> 16) & 0xFF);
            const i32 dg = g - static_cast<i32>((c >> 8) & 0xFF);
            const i32 db = b - static_cast<i32>(c & 0xFF);
            const i32 distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = static_cast<u8>(i);
            }
        }
        return best;
    }

    u8 TerrainDatabase::IndexOf(const std::string& id) const
    {
        for (size_t i = 0; i < m_types.size(); ++i)
        {
            if (m_types[i].id == id) return static_cast<u8>(i);
        }
        return 0;
    }
}
