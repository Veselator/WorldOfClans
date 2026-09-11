// RoadSystem.h - roads as roads, not as a building.
//
// A road is a path laid between two seats, and it is priced by the conceptual metre:
// how much earth had to be moved, plus a much steeper rate for every metre of river that
// had to be bridged. Where the route crosses water, the bridge is raised automatically -
// nobody orders a bridge, they order a road and pay for what the ground demands.
//
// The path itself comes from the same A* the armies march on, with a cost function that
// cares about slope and building effort rather than marching speed, so a road hugs the
// easy ground and crosses a river at its narrowest point.
#pragma once

#include "../../Core/Math.h"
#include "../../Core/Singleton.h"
#include "../Map/MapData.h"
#include "../../Render/RenderTypes.h"
#include "../World/ResourceData.h"

#include <string>
#include <vector>

namespace woc
{
    class World;

    /// A costed route, ready to be accepted or refused by whoever asked for it.
    struct RoadPlan
    {
        bool valid = false;
        std::string problem;

        std::vector<Coord> tiles;
        EntityId from = kInvalidId;
        EntityId to = kInvalidId;

        f32 metres = 0.0f;          // total length in conceptual metres
        f32 bridgeMetres = 0.0f;    // of which crossing water
        i32 days = 0;
        ResourceData cost;
    };

    /// Roadworks in progress: the track appears tile by tile as the season's work is done.
    struct RoadProject
    {
        EntityId clan = kInvalidId;
        std::vector<Coord> tiles;
        f32 daysTotal = 1.0f;
        f32 daysDone = 0.0f;
        size_t stamped = 0;
        std::string label;
    };

    class RoadSystem final : public Singleton<RoadSystem>
    {
        friend class Singleton<RoadSystem>;
    public:
        /// Costs a route between two holdings without changing anything.
        RoadPlan Plan(World& world, EntityId fromSettlement, EntityId toSettlement) const;
        /// Charges the clan and puts the route into the works. False if it cannot pay.
        bool Begin(World& world, EntityId clanId, const RoadPlan& plan);
        /// Advances every project; call once per simulated day.
        void Tick(World& world, f32 days);

        /// Writes the stored road segments into the tile grid. Used after loading a map.
        void StampExisting(World& world);
        /// Rebuilds the road geometry and hands it to the renderer. Cheap when unchanged.
        void UploadLayer(World& world);
        void MarkDirty() { m_dirty = true; }
        /// Forgets every project and cached layer. A new party inherits no roadworks.
        void Reset();

        const std::vector<RoadProject>& Projects() const { return m_projects; }
        /// Is there already a finished or ongoing road between these two holdings?
        bool AreConnected(const World& world, EntityId a, EntityId b) const;

        /// Metres per map unit, so the interface can quote the same figures.
        f32 MetresPerUnit() const;

    private:
        RoadSystem() = default;
        ~RoadSystem() = default;

        /// Effort of laying road across a tile: slope and forest cost work, water costs a bridge.
        static f32 BuildCost(const MapData& map, const Coord& tile);
        void Stamp(World& world, const Coord& tile);

        /// Rasterises every stretch of road into a full-resolution layer.
        void BuildLayer(const MapData& map, std::vector<u8>& mask) const;

        std::vector<RoadProject> m_projects;
        std::vector<u8> m_lastMask;
        bool m_dirty = true;
    };
}
