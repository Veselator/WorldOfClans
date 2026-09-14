// MovementSystem.h - marching, arrival and the orders that follow from it.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/Cohort.h"

namespace woc
{
    class World;
    class MapData;

    class MovementSystem final : public Singleton<MovementSystem>
    {
        friend class Singleton<MovementSystem>;
    public:
        /// Advances every cohort by `days` of marching.
        void Tick(World& world, f32 days);

        /// Issues a move order, running A* over the movement cost model.
        bool OrderMove(World& world, EntityId cohortId, const Vec2& destination);

        /// Peels the given units off into a new cohort standing beside the old one. Returns
        /// the new cohort, or kInvalidId when there is nothing sensible to split.
        EntityId Split(World& world, EntityId cohortId, const std::vector<EntityId>& units);
        /// Splits a host down the middle - the common case, and what the panel's button does.
        EntityId SplitInHalf(World& world, EntityId cohortId);
        /// Folds several hosts into the first of them. Everything that will fit under one
        /// banner marches under it; anything over the ten-unit limit stays where it is, so
        /// merging never quietly loses a detachment. Returns the surviving host.
        EntityId Merge(World& world, const std::vector<EntityId>& cohorts);
        /// Sends a host home: the units are struck off and the men go back to the fields
        /// they were called from, so the upkeep stops and the countryside gets its people
        /// back. There is no undoing it, which is why the panel asks twice.
        bool Disband(World& world, EntityId cohortId);
        /// Move and then perform an action on arrival.
        bool OrderTask(World& world, EntityId cohortId, TaskType type, const Vec2& destination,
                       EntityId targetSettlement = kInvalidId, EntityId targetCohort = kInvalidId);

        /// Marching speed in map pixels per day for a whole cohort (its slowest unit).
        f32 CohortSpeed(const World& world, EntityId cohortId) const;
        /// The pace the column is actually making right now, with the ground it stands on
        /// taken into account. This is what the panel shows and what the march consumes.
        f32 CurrentPace(const World& world, EntityId cohortId) const;

        /// Estimated days to reach a destination, for the order preview.
        f32 EstimateTravelDays(World& world, EntityId cohortId, const Vec2& destination);

        /// Sends every host that has been told to keep order at the nearest rising inside
        /// its own realm. A village that has thrown off its lord is unclaimed ground until
        /// somebody stands on it again, so the host besieges it exactly as it would any
        /// other holding - it simply chooses the target for itself.
        void AnswerRevolts(World& world);

    private:
        MovementSystem() = default;
        ~MovementSystem() = default;

        void AdvanceCohort(World& world, Cohort& cohort, f32 days);
        /// Cost of a map unit of march at this spot, never zero or negative.
        static f32 TerrainCost(const MapData& map, const Vec2& position);
        /// The nearest spot to `wanted` an army can actually stand on. A band of armies
        /// told to take up position around a point must spread over the ground as it is,
        /// not walk into a river because the ring said so.
        static Vec2 NearestStanding(const MapData& map, const Vec2& wanted, f32 searchRadius = 120.0f);
        void OnArrival(World& world, Cohort& cohort);
        /// Hosts ordered after an enemy host follow where it is now, not where it was when
        /// the order was given. Re-routes when the quarry has moved far enough from the
        /// point being marched on; gives up when it is gone or no longer an enemy.
        void UpdatePursuits(World& world);
        void UpdateSupply(World& world, Cohort& cohort, f32 days);
    };
}
