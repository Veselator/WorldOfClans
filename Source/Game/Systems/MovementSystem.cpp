#include "MovementSystem.h"
#include "CoverageSystem.h"
#include "../Map/Pathfinder.h"
#include "../World/World.h"
#include "../../Core/Config.h"

#include <algorithm>

namespace woc
{
    f32 MovementSystem::CohortSpeed(const World& world, EntityId cohortId) const
    {
        const Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort || cohort->units.empty()) return 0.0f;

        // A column moves at the pace of its slowest contingent.
        f32 slowest = 10.0f;
        for (EntityId unitId : cohort->units)
        {
            const Unit* unit = world.FindUnit(unitId);
            if (!unit || unit->IsDestroyed()) continue;
            slowest = std::min(slowest, unit->Stats().speed);
        }

        const f32 base = ConfigManager::Get().Float("movement/baseSpeed", 14.0f);
        const f32 supplyFactor = 0.6f + Clamp01(cohort->supply) * 0.4f;
        return base * slowest * supplyFactor;
    }

    bool MovementSystem::OrderMove(World& world, EntityId cohortId, const Vec2& destination)
    {
        return OrderTask(world, cohortId, TaskType::Move, destination);
    }

    bool MovementSystem::OrderTask(World& world, EntityId cohortId, TaskType type, const Vec2& destination,
                                   EntityId targetSettlement, EntityId targetCohort)
    {
        Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort) return false;

        const MapData& map = world.Map();
        const Coord start = map.ToTile(cohort->position);
        const Coord goal = map.ToTile(destination);

        PathResult path = Pathfinder::Get().FindPath(map, start, goal, &Pathfinder::MovementCost);
        if (!path.found)
        {
            // Fall back to the closest reachable point so an order is never silently dropped.
            return false;
        }

        cohort->currentTask.Clear();
        cohort->currentTask.type = type;
        cohort->currentTask.destination = destination;
        cohort->currentTask.targetSettlement = targetSettlement;
        cohort->currentTask.targetCohort = targetCohort;
        cohort->currentTask.waypoints = Pathfinder::ToWaypoints(map, path);
        if (!cohort->currentTask.waypoints.empty())
        {
            cohort->currentTask.waypoints.back() = destination;
        }
        cohort->currentTask.waypointIndex = 0;
        cohort->garrisonOf = kInvalidId;
        return true;
    }

    f32 MovementSystem::EstimateTravelDays(World& world, EntityId cohortId, const Vec2& destination)
    {
        const Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort) return 0.0f;

        const MapData& map = world.Map();
        PathResult path = Pathfinder::Get().FindPath(map, map.ToTile(cohort->position),
                                                     map.ToTile(destination), &Pathfinder::MovementCost);
        if (!path.found) return -1.0f;

        const f32 speed = CohortSpeed(world, cohortId);
        if (speed <= 0.0f) return -1.0f;

        // Path cost is in weighted tiles; convert to map pixels before dividing by speed.
        const f32 pixels = path.cost * static_cast<f32>(map.TilePixels());
        return pixels / speed;
    }

    void MovementSystem::Tick(World& world, f32 days)
    {
        if (days <= 0.0f) return;

        for (auto& [id, cohort] : world.Cohorts())
        {
            UpdateSupply(world, cohort, days);
            if (cohort.inBattle) continue;
            AdvanceCohort(world, cohort, days);
        }
    }

    void MovementSystem::UpdateSupply(World& world, Cohort& cohort, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();
        const Clan* clan = world.FindClan(cohort.clan);
        const EntityId owner = CoverageSystem::Get().OwnerAt(world, cohort.position);

        // Standing on friendly soil replenishes an army; enemy country wears it down.
        const bool friendly = clan && (owner == cohort.clan ||
            (owner != kInvalidId && !world.AreHostile(cohort.clan, owner)));

        const f32 rate = friendly ? 0.06f : -0.035f;
        cohort.supply = Clamp01(cohort.supply + rate * days);

        const f32 recovery = config.Float("movement/moraleRecoveryPerDay", 0.05f);
        const f32 fatigue = config.Float("movement/fatiguePerDay", 0.02f);
        const bool resting = cohort.currentTask.type == TaskType::Garrison ||
                             cohort.currentTask.type == TaskType::Idle;

        for (EntityId unitId : cohort.units)
        {
            Unit* unit = world.FindUnit(unitId);
            if (!unit) continue;

            if (resting)
            {
                unit->morale = Clamp01(unit->morale + recovery * days * (friendly ? 1.0f : 0.4f));
                unit->fatigue = Clamp01(unit->fatigue - fatigue * days * 2.0f);
            }
            else if (cohort.currentTask.IsMoving())
            {
                unit->fatigue = Clamp01(unit->fatigue + fatigue * days);
            }

            if (cohort.supply < 0.25f)
            {
                unit->morale = Clamp01(unit->morale - 0.02f * days);
            }
        }
    }

    void MovementSystem::AdvanceCohort(World& world, Cohort& cohort, f32 days)
    {
        Task& task = cohort.currentTask;
        if (!task.IsMoving()) return;

        f32 budget = CohortSpeed(world, cohort.id) * days;
        if (budget <= 0.0f) return;

        while (budget > 0.0f && task.waypointIndex < task.waypoints.size())
        {
            const Vec2 target = task.waypoints[task.waypointIndex];
            const Vec2 delta = target - cohort.position;
            const f32 distance = delta.Length();

            if (distance <= budget)
            {
                cohort.position = target;
                budget -= distance;
                ++task.waypointIndex;
            }
            else
            {
                cohort.position += delta.Normalized() * budget;
                budget = 0.0f;
            }
        }

        task.progressDays += days;
        if (task.waypointIndex >= task.waypoints.size())
        {
            OnArrival(world, cohort);
        }
    }

    void MovementSystem::OnArrival(World& world, Cohort& cohort)
    {
        Task& task = cohort.currentTask;
        task.waypoints.clear();
        task.waypointIndex = 0;

        switch (task.type)
        {
        case TaskType::Move:
            task.type = TaskType::Idle;
            break;

        case TaskType::Garrison:
            if (Settlement* settlement = world.FindSettlement(task.targetSettlement))
            {
                cohort.garrisonOf = settlement->id;
                cohort.position = settlement->position;
            }
            break;

        case TaskType::Besiege:
        case TaskType::Raid:
        case TaskType::Attack:
            // The battle system takes over from here; the order stays active.
            break;

        default:
            break;
        }
    }
}
