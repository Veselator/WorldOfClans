// ForestrySystem.h - forests and fields as living map layers.
//
// Woodland is not bolted to the terrain type: villages cut it back, sawmills cut it back
// faster, and it creeps outwards again from whatever is left standing. Fields spread from
// villages over good soil. Both are stored as density per tile and drawn as sprite masks.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"
#include "../../Core/Json.h"
#include "../World/ResourceData.h"

#include <string>
#include <vector>

namespace woc
{
    class World;

    /// A stand of young trees the peasants are putting in. Woodland does not appear the
    /// day it is paid for: the saplings go in, and then it is a matter of seasons.
    struct Plantation
    {
        EntityId clan = kInvalidId;
        Vec2 centre;
        f32 radius = 150.0f;
        f32 daysTotal = 1.0f;
        f32 daysDone = 0.0f;
        f32 target = 0.85f;     // density the stand grows towards
        std::string label;

        f32 Progress() const { return daysTotal > 0.0f ? Clamp01(daysDone / daysTotal) : 1.0f; }
    };

    /// What planting a wood on this spot would cost, before anybody has paid for it.
    struct PlantingPlan
    {
        bool valid = false;
        std::string problem;
        Vec2 centre;
        f32 radius = 0.0f;
        i32 tiles = 0;
        i32 days = 0;
        ResourceData cost;
    };

    class ForestrySystem final : public Singleton<ForestrySystem>
    {
        friend class Singleton<ForestrySystem>;
    public:
        void Tick(World& world);

        /// Pushes the current forest and field layers to the renderer.
        void UploadLayers(World& world);
        /// Wipes ploughland off ground that cannot be ploughed. A map painted by hand, or
        /// saved before the rule existed, may carry furrows across sand and hillside.
        void ClearUnarableFields(World& world);
        /// The same for woods. A forest stands on the three soils and nowhere else: not on
        /// the sand, not on the hillside, not in the highland. A hand-painted tree layer
        /// knows nothing of that rule, and a tile that straddles a boundary inherits both
        /// its neighbours' trees and the hill's ground when the map is read in.
        void ClearUnforestable(World& world);

        bool LayersDirty() const { return m_layersDirty; }
        void MarkLayersDirty() { m_layersDirty = true; }

        // --- planting a wood ------------------------------------------------------------
        /// What a stand of this much ground costs and how long it takes. `area` is in map
        /// units squared; pricing by area rather than by tile keeps the figures the same
        /// whatever resolution the map's simulation grid happens to use.
        static void PriceStand(f32 area, ResourceData& cost, i32& days);
        /// Prices a stand on this spot for this clan without changing anything.
        PlantingPlan PlanPlanting(World& world, EntityId clanId, const Vec2& centre) const;
        /// Charges the clan and sets the peasants to work. False if it cannot pay.
        bool BeginPlanting(World& world, EntityId clanId, const PlantingPlan& plan);
        /// Advances every stand; call once per simulated day.
        void TickPlantations(World& world, f32 days);
        const std::vector<Plantation>& Plantations() const { return m_plantations; }
        /// A new party inherits nobody's saplings.
        void ResetPlantations() { m_plantations.clear(); }

        Json ToJson() const;
        void FromJson(const Json& node);

    private:
        ForestrySystem() = default;
        ~ForestrySystem() = default;

        void Harvest(World& world);
        void Regrow(World& world);
        void GrowFields(World& world);

        std::vector<Plantation> m_plantations;
        bool m_layersDirty = true;
    };
}
