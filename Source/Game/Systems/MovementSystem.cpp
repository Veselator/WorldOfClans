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

    f32 MovementSystem::CurrentPace(const World& world, EntityId cohortId) const
    {
        const Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort) return 0.0f;

        const f32 cost = TerrainCost(world.Map(), cohort->position);
        return CohortSpeed(world, cohortId) / cost;
    }

    f32 MovementSystem::TerrainCost(const MapData& map, const Vec2& position)
    {
        // The same cost model the pathfinder marches on: hills and forest cost more, a road
        // costs half. A negative value means the tile is impassable - a column that somehow
        // stands on one should crawl off it, not freeze for ever.
        const f32 cost = map.MoveCost(map.ToTile(position));
        return cost > 0.0f ? cost : 2.5f;
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

    EntityId MovementSystem::Split(World& world, EntityId cohortId, const std::vector<EntityId>& units)
    {
        Cohort* source = world.FindCohort(cohortId);
        if (!source || units.empty()) return kInvalidId;
        if (units.size() >= source->units.size()) return kInvalidId;   // nothing would be left

        Clan* clan = world.FindClan(source->clan);
        if (!clan) return kInvalidId;

        Cohort& fresh = world.CreateCohort();
        fresh.clan = source->clan;
        fresh.name = source->DisplayName() + " (друга частина)";

        // The new host forms up a short march away, so the two are separate on the map.
        fresh.position = { source->position.x + 26.0f, source->position.y + 18.0f };

        // What the men carry with them: their training, their supplies, and a host that has
        // just been divided is by that much less in hand than it was.
        fresh.experience = source->experience;
        fresh.supply = source->supply;
        fresh.mayRaid = source->mayRaid;
        fresh.organisation = Clamp01(source->organisation *
            ConfigManager::Get().Float("movement/splitOrganisationCost", 0.75f));
        source->organisation = fresh.organisation;

        for (EntityId unitId : units)
        {
            const auto it = std::find(source->units.begin(), source->units.end(), unitId);
            if (it == source->units.end()) continue;
            source->units.erase(it);

            if (Unit* unit = world.FindUnit(unitId)) unit->cohort = fresh.id;
            fresh.units.push_back(unitId);
        }

        if (fresh.units.empty())
        {
            world.DestroyCohort(fresh.id);
            return kInvalidId;
        }

        clan->cohorts.push_back(fresh.id);

        // A detachment leaves the walls; it does not inherit the garrison's orders.
        fresh.garrisonOf = kInvalidId;
        fresh.currentTask.Clear();

        world.Log(source->DisplayName() + " ділиться надвоє", clan->color);
        return fresh.id;
    }

    void MovementSystem::AnswerRevolts(World& world)
    {
        CoverageSystem& coverage = CoverageSystem::Get();
        const i32 today = world.Time().TotalDays();

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (!cohort.suppressRevolts || cohort.IsEmpty() || cohort.IsWithdrawing()) continue;

            // Already on its way somewhere, or already sitting on a rising: leave it be.
            if (cohort.currentTask.type == TaskType::Besiege) continue;
            if (cohort.currentTask.type != TaskType::Idle &&
                cohort.currentTask.type != TaskType::Garrison) continue;

            const Clan* clan = world.FindClan(cohort.clan);
            const State* state = clan ? world.FindState(clan->state) : nullptr;
            if (!state) continue;

            const Settlement* nearest = nullptr;
            f32 bestDistance = ConfigManager::Get().Float("movement/revoltAnswerRange", 900.0f);

            for (const auto& [id, settlement] : world.Settlements())
            {
                // Only a fresh rising, and only one that is still standing on the realm's
                // own ground - a village that has drifted out of every border is somebody
                // else's problem now.
                if (!settlement.IsIndependent()) continue;
                if (today >= settlement.rebelliousUntilDay) continue;

                const EntityId holder = coverage.OwnerAt(world, settlement.position);
                const Clan* holdingClan = world.FindClan(holder);
                if (!holdingClan || holdingClan->state != state->id) continue;

                const f32 distance = Distance(cohort.position, settlement.position);
                if (distance >= bestDistance) continue;

                bestDistance = distance;
                nearest = &settlement;
            }

            if (!nearest) continue;

            if (OrderTask(world, cohortId, TaskType::Besiege, nearest->position, nearest->id))
            {
                cohort.garrisonOf = kInvalidId;
                world.Log(cohort.DisplayName() + " іде втихомирювати " + nearest->name,
                          clan->color);
            }
        }
    }

    bool MovementSystem::Disband(World& world, EntityId cohortId)
    {
        Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort) return false;

        const Clan* clan = world.FindClan(cohort->clan);
        const std::string name = cohort->DisplayName();

        // The men are not killed, they are sent home: whatever holding is nearest takes
        // them back into its population, which is where they came from in the first place.
        u32 released = 0;
        for (EntityId unitId : cohort->units)
        {
            const Unit* unit = world.FindUnit(unitId);
            if (unit) released += unit->Strength();
        }

        if (released > 0)
        {
            if (Settlement* home = world.NearestSettlement(cohort->position, 1e9f, cohort->clan))
            {
                home->population += static_cast<i32>(released);
            }
        }

        world.DestroyCohort(cohortId);
        world.Log(name + " розпущено" + (released > 0 ? " (" + std::to_string(released) + " чол. по домівках)" : ""),
                  clan ? clan->color : Color(0.8f, 0.8f, 0.8f, 1.0f));
        return true;
    }

    EntityId MovementSystem::SplitInHalf(World& world, EntityId cohortId)
    {
        const Cohort* source = world.FindCohort(cohortId);
        if (!source || source->units.size() < 2) return kInvalidId;

        const size_t half = source->units.size() / 2;
        const std::vector<EntityId> taken(source->units.end() - static_cast<long>(half),
                                          source->units.end());
        return Split(world, cohortId, taken);
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

        // Order returns on its own, faster at rest and faster still at home. A large host
        // mends more slowly: `organisationSizeReference` men recover at the base rate, and
        // twice that many at roughly half of it.
        {
            f32 rate = config.Float("movement/organisationPerDay", 0.055f);
            if (cohort.currentTask.IsMoving()) rate *= config.Float("movement/organisationMarchFactor", 0.35f);
            if (friendly) rate *= config.Float("movement/organisationHomeBonus", 1.6f);

            const f32 reference = std::max(1.0f, config.Float("movement/organisationSizeReference", 250.0f));
            const f32 strength = static_cast<f32>(world.CohortStrength(cohort.id));
            rate *= reference / std::max(reference, strength);

            cohort.organisation = Clamp01(cohort.organisation + rate * days);
        }

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

        // The budget is measured in *effort*, not in distance: a map unit of hillside or
        // forest costs more of it than a map unit of road, so the same army crosses the
        // plain in half the time it takes to climb the same span of ridge.
        f32 budget = CohortSpeed(world, cohort.id) * days;
        if (budget <= 0.0f) return;

        const MapData& map = world.Map();
        while (budget > 0.0f && task.waypointIndex < task.waypoints.size())
        {
            const f32 cost = TerrainCost(map, cohort.position);

            const Vec2 target = task.waypoints[task.waypointIndex];
            const Vec2 delta = target - cohort.position;
            const f32 distance = delta.Length();
            const f32 affordable = budget / cost;

            if (distance <= affordable)
            {
                cohort.position = target;
                budget -= distance * cost;
                ++task.waypointIndex;
            }
            else
            {
                cohort.position += delta.Normalized() * affordable;
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
