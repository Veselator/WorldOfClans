#include "MovementSystem.h"
#include "CoverageSystem.h"
#include "../Map/Pathfinder.h"
#include "../World/World.h"
#include "../../Core/Config.h"

#include <algorithm>
#include <cmath>

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
        // Men running from a lost field cover ground they could never march in good order.
        const f32 flight = cohort->retreating ? ConfigManager::Get().Float("battle/retreatSpeedFactor", 2.2f) : 1.0f;
        return base * slowest * supplyFactor * flight;
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

    Vec2 MovementSystem::NearestStanding(const MapData& map, const Vec2& wanted, f32 searchRadius)
    {
        if (map.IsPassable(map.ToTile(wanted))) return wanted;

        // Spiral outwards a tile at a time and take the first ground a column could stand
        // on. Cheap, and it always answers with somewhere near where the order was given.
        const f32 step = static_cast<f32>(std::max(1u, map.TilePixels()));
        const i32 rings = std::max(1, static_cast<i32>(searchRadius / step));

        for (i32 ring = 1; ring <= rings; ++ring)
        {
            const i32 samples = ring * 8;
            for (i32 i = 0; i < samples; ++i)
            {
                const f32 angle = static_cast<f32>(i) / static_cast<f32>(samples) * 2.0f * kPi;
                const Vec2 probe{ wanted.x + std::cos(angle) * step * ring,
                                  wanted.y + std::sin(angle) * step * ring };
                const Coord tile = map.ToTile(probe);
                if (map.InBounds(tile) && map.IsPassable(tile)) return probe;
            }
        }
        return wanted;
    }

    bool MovementSystem::OrderMove(World& world, EntityId cohortId, const Vec2& destination)
    {
        return OrderTask(world, cohortId, TaskType::Move, destination);
    }

    EntityId MovementSystem::Merge(World& world, const std::vector<EntityId>& cohorts)
    {
        if (cohorts.size() < 2) return kInvalidId;

        Cohort* host = world.FindCohort(cohorts.front());
        if (!host) return kInvalidId;

        Clan* clan = world.FindClan(host->clan);
        if (!clan) return kInvalidId;

        const u32 limit = UnitDatabase::Get().MaxUnitsPerCohort();
        u32 absorbed = 0;
        std::vector<EntityId> emptied;

        for (size_t i = 1; i < cohorts.size(); ++i)
        {
            Cohort* other = world.FindCohort(cohorts[i]);
            if (!other || other->id == host->id) continue;
            if (other->clan != host->clan) continue;      // one banner, one house
            // Two hosts become one by standing in the same field, not by an order sent
            // across the map: whoever is too far away is simply left out.
            if (Distance(other->position, host->position) >
                ConfigManager::Get().Float("movement/mergeDistance", 40.0f)) continue;

            // What the two hosts bring: the joined army is as well supplied and as well
            // ordered as the worse of them, because a column is only as good as its tail.
            host->supply = std::min(host->supply, other->supply);
            host->organisation = std::min(host->organisation, other->organisation);
            host->experience = std::max(host->experience, other->experience);

            while (!other->units.empty() && host->units.size() < limit)
            {
                const EntityId unitId = other->units.back();
                other->units.pop_back();
                if (Unit* unit = world.FindUnit(unitId)) unit->cohort = host->id;
                host->units.push_back(unitId);
                ++absorbed;
            }

            if (other->units.empty()) emptied.push_back(other->id);
        }

        if (absorbed == 0) return kInvalidId;

        // Forming up as one body takes a moment, exactly as dividing does.
        host->organisation = Clamp01(host->organisation *
            ConfigManager::Get().Float("movement/mergeOrganisationCost", 0.9f));
        host->currentTask.Clear();

        for (EntityId id : emptied) world.DestroyCohort(id);

        world.Log(host->DisplayName() + ": війська зведено докупи (" +
                  std::to_string(host->units.size()) + " підрозділів)", clan->color);
        return host->id;
    }

    bool MovementSystem::OrderTask(World& world, EntityId cohortId, TaskType type, const Vec2& destination,
                                   EntityId targetSettlement, EntityId targetCohort)
    {
        Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort) return false;

        const MapData& map = world.Map();
        const Coord start = map.ToTile(cohort->position);

        // Armies stand on ground, not on coordinates. A band of hosts spread into a ring
        // around a point will inevitably put one of them on water or on a cliff; that host
        // takes the nearest standing room instead of refusing to march at all.
        const Vec2 target = NearestStanding(map, destination);
        const Coord goal = map.ToTile(target);

        PathResult path = Pathfinder::Get().FindPath(map, start, goal, &Pathfinder::MovementCost);
        if (!path.found) return false;

        cohort->currentTask.Clear();
        cohort->retreating = false;   // a fresh order is marched, not run
        cohort->currentTask.type = type;
        cohort->currentTask.destination = target;
        cohort->currentTask.targetSettlement = targetSettlement;
        cohort->currentTask.targetCohort = targetCohort;
        cohort->currentTask.waypoints = Pathfinder::ToWaypoints(map, path);
        if (!cohort->currentTask.waypoints.empty())
        {
            cohort->currentTask.waypoints.back() = target;
        }
        cohort->currentTask.waypointIndex = 0;
        cohort->garrisonOf = kInvalidId;
        // The walls it was sent against are remembered apart from the order, so a relief
        // army that interrupts the siege does not make the host forget what it came for.
        cohort->siegeTarget = type == TaskType::Besiege ? targetSettlement : kInvalidId;
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

        // Every chaser reads the positions as they stand before anybody moves this tick,
        // so who is processed first makes no difference.
        UpdatePursuits(world);

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

        // Standing on friendly soil is not the same as being fed by it. An army lives off
        // its own country's barns and off an ally's, because somebody has agreed to open
        // them; nobody's land has no barns to open, and a neutral's are shut. This is the
        // difference between marching through a country and being welcome in it.
        const bool friendly = clan && (owner == cohort.clan ||
            (owner != kInvalidId && !world.AreHostile(cohort.clan, owner)));

        bool supplied = clan && owner == cohort.clan;
        if (clan && !supplied && owner != kInvalidId)
        {
            const State* ours = world.StateOfClan(cohort.clan);
            const State* theirs = world.StateOfClan(owner);
            supplied = ours && theirs &&
                       (ours->id == theirs->id || ours->IsAlliedWith(theirs->id));
        }

        const f32 rate = supplied ? 0.06f : -0.035f;
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

            // Drill. A recruit is not a soldier; he becomes one over months of it, and the
            // mounted arms take the longest because there is a horse to bring on too. Men
            // learn fastest standing behind their own walls with nothing else to do, more
            // slowly in camp, and hardly at all on the march.
            {
                const f32 span = std::max(1.0f, unit->Stats().trainDays);
                f32 rate = 1.0f / span;
                if (cohort.garrisonOf != kInvalidId) rate *= config.Float("movement/trainingGarrisonBonus", 1.5f);
                else if (cohort.currentTask.IsMoving()) rate *= config.Float("movement/trainingMarchFactor", 0.2f);
                else rate *= config.Float("movement/trainingCampFactor", 0.6f);

                // Half-starved men do not drill.
                rate *= 0.4f + Clamp01(cohort.supply) * 0.6f;
                unit->training = std::min(1.0f, unit->training + rate * days);
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

    void MovementSystem::UpdatePursuits(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 repath = config.Float("movement/pursuitRepathDistance", 8.0f);
        const f32 contact = config.Float("battle/engagementRadius", 18.0f) * 0.6f;

        std::vector<std::pair<EntityId, EntityId>> chases;   // chaser, quarry
        for (const auto& [id, cohort] : world.Cohorts())
        {
            if (cohort.currentTask.targetCohort == kInvalidId || cohort.inBattle) continue;
            chases.emplace_back(id, cohort.currentTask.targetCohort);
        }
        std::sort(chases.begin(), chases.end());

        for (const auto& [chaserId, quarryId] : chases)
        {
            Cohort* chaser = world.FindCohort(chaserId);
            if (!chaser) continue;
            const Cohort* quarry = world.FindCohort(quarryId);

            // Nothing left to chase: destroyed, or peace has been made with it.
            if (!quarry || quarry->IsEmpty() || !world.AreHostile(chaser->clan, quarry->clan))
            {
                chaser->currentTask.Clear();
                continue;
            }

            const f32 gap = Distance(chaser->position, quarry->position);
            const bool drifted = Distance(chaser->currentTask.destination, quarry->position) > repath;
            const bool standing = !chaser->currentTask.IsMoving();
            if (!drifted && !(standing && gap > contact)) continue;

            // Close in straight when it is near; round the ground when it is not.
            const Vec2 goal = quarry->position;
            if (gap < 40.0f && world.Map().MoveCost(world.Map().ToTile(goal)) > 0.0f)
            {
                Task& task = chaser->currentTask;
                task.destination = goal;
                task.waypoints = { goal };
                task.waypointIndex = 0;
                continue;
            }
            // No way to it at all (across water, say): the chase is off rather than
            // searched for again every tick.
            if (!OrderTask(world, chaserId, chaser->currentTask.type, goal, kInvalidId, quarryId))
            {
                chaser->currentTask.Clear();
            }
        }
    }

    void MovementSystem::OnArrival(World& world, Cohort& cohort)
    {
        Task& task = cohort.currentTask;
        task.waypoints.clear();
        task.waypointIndex = 0;
        cohort.retreating = false;

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
        case TaskType::Storm:
            // The battle system - or, for a robbers' camp, the bandit system - takes over
            // from here; the order stays active.
            break;

        default:
            break;
        }
    }
}
