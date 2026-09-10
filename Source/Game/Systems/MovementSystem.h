// MovementSystem.h - marching, arrival and the orders that follow from it.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/Cohort.h"

namespace woc
{
    class World;

    class MovementSystem final : public Singleton<MovementSystem>
    {
        friend class Singleton<MovementSystem>;
    public:
        /// Advances every cohort by `days` of marching.
        void Tick(World& world, f32 days);

        /// Issues a move order, running A* over the movement cost model.
        bool OrderMove(World& world, EntityId cohortId, const Vec2& destination);
        /// Move and then perform an action on arrival.
        bool OrderTask(World& world, EntityId cohortId, TaskType type, const Vec2& destination,
                       EntityId targetSettlement = kInvalidId, EntityId targetCohort = kInvalidId);

        /// Marching speed in map pixels per day for a whole cohort (its slowest unit).
        f32 CohortSpeed(const World& world, EntityId cohortId) const;

        /// Estimated days to reach a destination, for the order preview.
        f32 EstimateTravelDays(World& world, EntityId cohortId, const Vec2& destination);

    private:
        MovementSystem() = default;
        ~MovementSystem() = default;

        void AdvanceCohort(World& world, Cohort& cohort, f32 days);
        void OnArrival(World& world, Cohort& cohort);
        void UpdateSupply(World& world, Cohort& cohort, f32 days);
    };
}
