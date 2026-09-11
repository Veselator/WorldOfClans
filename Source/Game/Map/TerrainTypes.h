// TerrainTypes.h - terrain palette loaded from Config/terrain.json.
//
// The map is authored as an indexed-colour PNG; this database is what turns a pixel
// colour into movement cost, soil quality, defensive value and so on. Adding a terrain
// is therefore a data change, not a code change.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"

namespace woc
{
    struct TerrainInfo
    {
        std::string id;
        std::string name;
        u32 color = 0;
        bool passable = true;
        bool water = false;
        bool buildable = true;
        f32 moveCost = 1.0f;
        f32 coverageCost = 1.0f;
        f32 soil = 1.0f;
        /// Whether a plough can be put to this ground at all. Sand, hillside and highland
        /// carry some soil value for other purposes, but no village tills them.
        bool arable = false;
        /// Whether a quarry can be sunk here. Rock lies under the hills and the highland;
        /// the bare peaks have plenty of it and no way in.
        bool mineable = false;
        f32 defense = 1.0f;
        f32 stone = 0.0f;
        f32 heightFactor = 0.5f;
    };

    class TerrainDatabase final : public Singleton<TerrainDatabase>
    {
        friend class Singleton<TerrainDatabase>;
    public:
        void Load();

        size_t Count() const { return m_types.size(); }
        const TerrainInfo& At(u8 index) const;
        const std::vector<TerrainInfo>& All() const { return m_types; }

        /// Index of the terrain whose palette colour is closest to `rgb`.
        u8 MatchColor(u32 rgb) const;
        /// Index of a terrain by its string id, or 0 when unknown.
        u8 IndexOf(const std::string& id) const;

        f32 FordableMoveCost() const { return m_fordableMoveCost; }
        f32 FordableCoverageCost() const { return m_fordableCoverageCost; }
        f32 HeightScale() const { return m_heightScale; }

    private:
        TerrainDatabase() = default;
        ~TerrainDatabase() = default;

        std::vector<TerrainInfo> m_types;
        f32 m_fordableMoveCost = 4.5f;
        f32 m_fordableCoverageCost = 3.5f;
        f32 m_heightScale = 42.0f;
    };
}
