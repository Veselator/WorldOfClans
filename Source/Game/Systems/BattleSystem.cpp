#include "BattleSystem.h"
#include "CoverageSystem.h"
#include "MovementSystem.h"
#include "../Factories/CharacterFactory.h"
#include "../Factories/EvaluatorFactory.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Random.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        /// Average counter multiplier of one role against an enemy cohort's actual mix.
        f32 CounterAgainst(const World& world, UnitRole role, const Cohort* enemy)
        {
            if (!enemy) return 1.0f;

            const UnitDatabase& db = UnitDatabase::Get();
            f32 weighted = 0.0f;
            f32 total = 0.0f;
            for (EntityId unitId : enemy->units)
            {
                const Unit* unit = world.FindUnit(unitId);
                if (!unit || unit->IsDestroyed()) continue;
                const f32 weight = static_cast<f32>(unit->Strength());
                weighted += db.Counter(role, unit->role) * weight;
                total += weight;
            }
            return total > 0.0f ? weighted / total : 1.0f;
        }

        /// Extra bite for the side holding the high ground.
        f32 HeightAdvantage(const World& world, const Vec2& mine, const Vec2& theirs)
        {
            const MapData& map = world.Map();
            const f32 difference = map.WorldHeightAtMap(mine) - map.WorldHeightAtMap(theirs);
            const f32 perUnit = ConfigManager::Get().Float("battle/heightAdvantagePerUnit", 0.012f);
            return std::clamp(1.0f + difference * perUnit, 0.7f, 1.45f);
        }

        /// The aristocrat in a cohort lends his command to everyone else.
        f32 CommandBonus(const World& world, const Cohort& cohort)
        {
            f32 bonus = 0.0f;
            for (EntityId unitId : cohort.units)
            {
                const Unit* unit = world.FindUnit(unitId);
                if (!unit || unit->IsDestroyed()) continue;
                if (unit->role != UnitRole::Aristocrat) continue;
                bonus = std::max(bonus, unit->Stats().commandBonus);
            }
            return 1.0f + bonus;
        }
    }

    std::vector<EntityId> BattleSide::Hosts() const
    {
        std::vector<EntityId> hosts;
        hosts.reserve(support.size() + 1);
        if (cohort != kInvalidId) hosts.push_back(cohort);
        for (EntityId id : support) hosts.push_back(id);
        return hosts;
    }

    bool BattleSide::Includes(EntityId cohortId) const
    {
        if (cohortId == cohort) return true;
        return std::find(support.begin(), support.end(), cohortId) != support.end();
    }

    f32 BattleSystem::SidePower(const World& world, const BattleSide& side, const BattleSide& enemy) const
    {
        f32 total = 0.0f;
        for (EntityId id : side.Hosts())
        {
            const Cohort* host = world.FindCohort(id);
            if (!host || host->IsEmpty()) continue;
            total += EvaluatePower(world, id, enemy.cohort, host->position);
        }
        return total;
    }

    u32 BattleSystem::SideStrength(const World& world, const BattleSide& side) const
    {
        u32 total = 0;
        for (EntityId id : side.Hosts()) total += world.CohortStrength(id);
        return total;
    }

    void BattleSystem::SpreadDamage(World& world, BattleSide& side, f32 damage, u32& deadOut,
                                    u32& hurtOut, f32 killShare)
    {
        if (damage <= 0.0f) return;

        const std::vector<EntityId> hosts = side.Hosts();
        f32 total = 0.0f;
        for (EntityId id : hosts) total += static_cast<f32>(world.CohortStrength(id));
        if (total <= 0.0f) return;

        // Every host takes the share of the blow that matches the share of the line it is
        // holding. A small detachment beside a great host does not soak up half the battle.
        for (EntityId id : hosts)
        {
            Cohort* host = world.FindCohort(id);
            if (!host || host->IsEmpty()) continue;

            const f32 share = static_cast<f32>(world.CohortStrength(id)) / total;
            u32 dead = 0, hurt = 0;
            ApplyDamage(world, *host, damage * share, dead, hurt, killShare);
            deadOut += dead;
            hurtOut += hurt;
        }
    }

    f32 BattleSystem::EvaluatePower(const World& world, EntityId cohortId, EntityId enemyCohortId,
                                    const Vec2& position) const
    {
        const Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort) return 0.0f;

        const Cohort* enemy = world.FindCohort(enemyCohortId);
        const UnitDatabase& db = UnitDatabase::Get();
        const TerrainInfo& terrain = world.Map().TerrainAtMap(position);

        f32 total = 0.0f;
        for (EntityId unitId : cohort->units)
        {
            const Unit* unit = world.FindUnit(unitId);
            if (!unit || unit->IsDestroyed()) continue;
            if (unit->role == UnitRole::Aristocrat) continue;   // he commands, he does not hold the line

            f32 power = unit->CombatPower(cohort->experience);
            power *= db.TerrainAffinity(unit->role, terrain.id);
            power *= CounterAgainst(world, unit->role, enemy);
            total += power;
        }

        total *= CommandBonus(world, *cohort);
        if (enemy) total *= HeightAdvantage(world, position, enemy->position);
        total *= 0.7f + Clamp01(cohort->supply) * 0.3f;

        // A host that has lost its order fights at a fraction of its strength, however many
        // men it still counts. This is what makes a fresh, smaller army beat a big one that
        // has just force-marched across a river.
        const f32 floor = ConfigManager::Get().Float("battle/organisationFloor", 0.45f);
        total *= floor + Clamp01(cohort->organisation) * (1.0f - floor);
        return total;
    }

    f32 BattleSystem::SettlementDefense(World& world, EntityId settlementId) const
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return 0.0f;

        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(*settlement, world.Map());
        const f32 wallWeight = ConfigManager::Get().Float("battle/siegeWallWeight", 0.9f);

        // The walls themselves, plus whatever militia the population can raise.
        f32 defense = evaluator->Defense() * (1.0f + wallWeight);
        defense *= 1.0f + static_cast<f32>(settlement->population) / 3000.0f;

        // Garrisoned cohorts add their strength to the defence.
        for (const auto& [id, cohort] : world.Cohorts())
        {
            if (cohort.garrisonOf != settlementId) continue;
            defense += world.CohortPower(id) * 0.01f;
        }
        return defense;
    }

    void BattleSystem::Tick(World& world, f32 days)
    {
        if (days <= 0.0f) return;
        ResolveFieldBattles(world, days);
        ResolveSieges(world, days);
        ResolveRaids(world);
        TickRecovery(world, days);
    }

    const BattleReport* BattleSystem::BattleOf(EntityId cohortId) const
    {
        for (const BattleReport& battle : m_active)
        {
            if (battle.attacker.Includes(cohortId) || battle.defender.Includes(cohortId)) return &battle;
        }
        return nullptr;
    }

    bool BattleSystem::FindRetreat(const World& world, const Cohort& cohort, const Vec2& threat,
                                   f32 distance, Vec2& out) const
    {
        const MapData& map = world.Map();
        if (!map.IsValid()) return false;

        Vec2 away = cohort.position - threat;
        if (away.LengthSq() < 0.0001f) away = { 1.0f, 0.0f };
        away = away.Normalized();
        const f32 baseAngle = std::atan2(away.y, away.x);

        // Straight back first, then wider and wider to either side - but never past a
        // right angle and a bit, because the way out does not lie through the enemy.
        const f32 sweep = ConfigManager::Get().Float("battle/retreatSweepDegrees", 110.0f) *
                          kPi / 180.0f;
        const f32 stepAngle = sweep / 6.0f;

        // And if the full distance is blocked, a shorter step still counts as giving ground.
        const f32 tries[] = { 1.0f, 0.7f, 0.45f };

        for (f32 reach : tries)
        {
            for (i32 step = 0; step <= 6; ++step)
            {
                for (i32 sign = (step == 0 ? 0 : -1); sign <= 1; sign += 2)
                {
                    const f32 angle = baseAngle + static_cast<f32>(sign * step) * stepAngle;
                    const Vec2 candidate{
                        cohort.position.x + std::cos(angle) * distance * reach,
                        cohort.position.y + std::sin(angle) * distance * reach
                    };

                    const Coord tile = map.ToTile(candidate);
                    if (!map.InBounds(tile)) continue;
                    if (map.MoveCost(tile) <= 0.0f) continue;          // sea, cliff, bog
                    // Falling back towards the enemy is not falling back.
                    if (DistanceSq(candidate, threat) <= DistanceSq(cohort.position, threat)) continue;

                    out = candidate;
                    return true;
                }
                if (step == 0) continue;
            }
        }
        return false;
    }

    void BattleSystem::FallBack(World& world, Cohort& host, const Vec2& threat, f32 distance)
    {
        Vec2 landing;
        if (FindRetreat(world, host, threat, distance, landing))
        {
            BeginRetreat(world, host, landing);
            return;
        }

        // Nowhere to go. A host with the enemy in front of it and nothing behind it fights
        // where it stands, and what is left of it does not walk off the field.
        const Clan* clan = world.FindClan(host.clan);
        world.Log(host.DisplayName() + ": оточено, відступати нікуди — військо полягло",
                  clan ? clan->color : Color::FromRGB(0xB0413E));
        world.DestroyCohort(host.id);
    }

    void BattleSystem::BeginRetreat(World& world, Cohort& host, const Vec2& landing)
    {
        MovementSystem& movement = MovementSystem::Get();
        // Round the obstacle if there is one; if the path finder cannot help, straight at it -
        // FindRetreat already made sure the ground there holds.
        if (!movement.OrderTask(world, host.id, TaskType::Move, landing))
        {
            host.currentTask.Clear();
            host.currentTask.type = TaskType::Move;
            host.currentTask.destination = landing;
            host.currentTask.waypoints = { landing };
            host.currentTask.waypointIndex = 0;
            host.garrisonOf = kInvalidId;
        }
        host.retreating = true;
        host.inBattle = false;

        // Out of reach for the length of the run, and a little over.
        const f32 speed = movement.CohortSpeed(world, host.id);
        const f32 travel = speed > 0.0f ? Distance(host.position, landing) / speed : 0.0f;
        host.disengageDays = std::max(host.disengageDays, travel + 0.5f);
    }

    bool BattleSystem::IsAnnihilated(World& world, const Cohort& host, f32 organisationBefore,
                                     f32 decisiveness) const
    {
        ConfigManager& config = ConfigManager::Get();
        u32 strength = 0, full = 0;
        for (EntityId unitId : host.units)
        {
            const Unit* unit = world.FindUnit(unitId);
            if (!unit) continue;
            strength += unit->Strength();
            full += unit->establishment;
        }
        // Nobody left on his feet: the wounded are carried off by the enemy, not away.
        if (strength == 0) return true;
        const f32 fraction = full > 0 ? static_cast<f32>(strength) / static_cast<f32>(full) : 1.0f;
        if (fraction <= config.Float("battle/routCollapseStrength", 0.25f)) return true;
        // Broken once already and not yet rallied: the second rout is the last.
        if (organisationBefore <= config.Float("battle/shatterOrganisation", 0.2f)) return true;
        return decisiveness >= config.Float("battle/annihilationRatio", 4.0f);
    }

    void BattleSystem::CullBrokenHosts(World& world, BattleReport& report)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 orderFloor = config.Float("battle/collapseOrganisation", 0.08f);
        const f32 strengthFloor = config.Float("battle/collapseStrength", 0.12f);

        for (BattleSide* side : { &report.attacker, &report.defender })
        {
            for (EntityId id : side->Hosts())
            {
                Cohort* host = world.FindCohort(id);
                if (!host || host->IsEmpty()) continue;

                // Few men and no order left to hold them: this is not a host any more.
                u32 strength = 0, full = 0;
                for (EntityId unitId : host->units)
                {
                    const Unit* unit = world.FindUnit(unitId);
                    if (!unit) continue;
                    strength += unit->Strength();
                    full += unit->establishment;
                }
                if (full == 0) continue;

                const f32 fraction = static_cast<f32>(strength) / static_cast<f32>(full);
                // Nobody standing at all is the end of it whatever the order says.
                if (strength > 0 && (host->organisation > orderFloor || fraction > strengthFloor)) continue;

                const Clan* clan = world.FindClan(host->clan);
                world.Log(host->DisplayName() + " перестає існувати як військо",
                          clan ? clan->color : Color::FromRGB(0xB0413E));
                side->losses += strength;
                world.DestroyCohort(id);
            }
        }
    }

    namespace
    {
        Json SideToJson(const BattleSide& side)
        {
            Json node = Json::MakeObject();
            node["cohort"] = EncodeId(side.cohort);
            node["clan"] = EncodeId(side.clan);
            node["name"] = side.name;
            node["start"] = static_cast<i64>(side.startingStrength);
            node["losses"] = static_cast<i64>(side.losses);
            node["hurt"] = static_cast<i64>(side.hurt);
            node["power"] = side.power;
            node["routed"] = side.routed;
            node["withdrew"] = side.withdrew;
            node["support"] = EncodeIdList(side.support);
            node["flanked"] = side.flanked;
            return node;
        }

        BattleSide SideFromJson(const Json& node)
        {
            BattleSide side;
            side.cohort = DecodeId(node["cohort"]);
            side.clan = DecodeId(node["clan"]);
            side.name = node["name"].AsString();
            side.startingStrength = static_cast<u32>(node["start"].AsInt(0));
            side.losses = static_cast<u32>(node["losses"].AsInt(0));
            side.hurt = static_cast<u32>(node["hurt"].AsInt(0));
            side.power = node["power"].AsFloat(0.0f);
            side.routed = node["routed"].AsBool(false);
            side.withdrew = node["withdrew"].AsBool(false);
            side.support = DecodeIdList(node["support"]);
            side.flanked = node["flanked"].AsFloat(0.0f);
            return side;
        }
    }

    Json BattleSystem::ToJson() const
    {
        Json root = Json::MakeObject();
        root["recoveryCarry"] = m_recoveryCarry;
        Json list = Json::MakeArray();
        for (const BattleReport& battle : m_active)
        {
            Json node = Json::MakeObject();
            node["day"] = battle.day;
            node["x"] = battle.position.x;
            node["y"] = battle.position.y;
            node["attacker"] = SideToJson(battle.attacker);
            node["defender"] = SideToJson(battle.defender);
            node["terrain"] = battle.terrainName;
            node["concluded"] = battle.concluded;
            node["victor"] = EncodeId(battle.victor);
            node["round"] = battle.roundProgress;
            node["elapsed"] = battle.elapsedDays;
            list.Push(node);
        }
        root["active"] = list;
        return root;
    }

    void BattleSystem::FromJson(const Json& root)
    {
        m_active.clear();
        m_recoveryCarry = root["recoveryCarry"].AsFloat(0.0f);
        for (const Json& node : root["active"].AsArray())
        {
            BattleReport battle;
            battle.day = node["day"].AsInt(0);
            battle.position = { node["x"].AsFloat(0.0f), node["y"].AsFloat(0.0f) };
            battle.attacker = SideFromJson(node["attacker"]);
            battle.defender = SideFromJson(node["defender"]);
            battle.terrainName = node["terrain"].AsString();
            battle.concluded = node["concluded"].AsBool(false);
            battle.victor = DecodeId(node["victor"]);
            battle.roundProgress = node["round"].AsFloat(0.0f);
            battle.elapsedDays = node["elapsed"].AsFloat(0.0f);
            m_active.push_back(std::move(battle));
        }
    }

    bool BattleSystem::Withdraw(World& world, EntityId cohortId)
    {
        Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort) return false;

        ConfigManager& config = ConfigManager::Get();

        // Breaking contact under an enemy's nose is not free: the rearmost ranks pay for it,
        // and the host arrives wherever it is going in no state to fight.
        const BattleReport* battle = BattleOf(cohortId);
        if (battle)
        {
            const EntityId enemyId = battle->attacker.Includes(cohortId)
                                   ? battle->defender.cohort : battle->attacker.cohort;
            if (const Cohort* enemy = world.FindCohort(enemyId))
            {
                u32 dead = 0, hurt = 0;
                ApplyDamage(world, *cohort,
                            EvaluatePower(world, enemy->id, cohort->id, enemy->position) *
                            config.Float("battle/baseDamage", 0.06f) *
                            config.Float("battle/withdrawPartingBlow", 0.8f),
                            dead, hurt, config.Float("battle/withdrawKillShare", 0.5f));

                // Onto ground that will hold them. A host that "withdrew" into the sea
                // was the old behaviour, and a host with nowhere at all to withdraw to is
                // surrounded - it does not get to leave.
                Vec2 landing;
                if (!FindRetreat(world, *cohort, enemy->position,
                                 config.Float("battle/withdrawDistance", 55.0f), landing))
                {
                    const Clan* trapped = world.FindClan(cohort->clan);
                    world.Log(cohort->DisplayName() + ": відступати нікуди — оточено",
                              trapped ? trapped->color : Color::FromRGB(0xB0413E));
                    return false;
                }
                BeginRetreat(world, *cohort, landing);
            }
        }

        cohort->organisation = Clamp01(cohort->organisation -
                                       config.Float("battle/withdrawOrganisation", 0.25f));
        cohort->disengageDays = std::max(cohort->disengageDays, config.Float("battle/disengageDays", 3.0f));
        cohort->inBattle = false;
        if (!cohort->retreating) cohort->currentTask.Clear();

        // A host that came up alongside and then thought better of it simply leaves the
        // line; the fight goes on without it. Only when the side's own banner walks away
        // does the whole thing need untangling, and the next tick does that by itself.
        for (BattleReport& active : m_active)
        {
            for (BattleSide* side : { &active.attacker, &active.defender })
            {
                side->support.erase(std::remove(side->support.begin(), side->support.end(), cohortId),
                                    side->support.end());
            }
        }

        const Clan* clan = world.FindClan(cohort->clan);
        world.Log(cohort->DisplayName() + " виходить з бою",
                  clan ? clan->color : Color::FromRGB(0x9AA3AB));
        return true;
    }

    void BattleSystem::ReinforceGarrisons(World& world, i32 days)
    {
        if (days <= 0) return;

        ConfigManager& config = ConfigManager::Get();
        const UnitDatabase& db = UnitDatabase::Get();
        Random& random = GlobalRandom();

        // What a settlement can give up in a day, and what it costs the treasury per head.
        const f32 sharePerDay = config.Float("recruitment/garrisonRefillPerDay", 0.02f);
        const i32 populationFloor = config.Int("recruitment/garrisonPopulationFloor", 160);
        const f32 headCost = config.Float("recruitment/refillCostPerHead", 0.45f);

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.garrisonOf == kInvalidId) continue;
            if (cohort.inBattle) continue;

            Settlement* home = world.FindSettlement(cohort.garrisonOf);
            if (!home || home->owner != cohort.clan) continue;
            if (home->besiegedBy != kInvalidId || home->UnderConstruction()) continue;
            if (home->population <= populationFloor) continue;

            Clan* clan = world.FindClan(cohort.clan);
            if (!clan) continue;

            for (EntityId unitId : cohort.units)
            {
                Unit* unit = world.FindUnit(unitId);
                if (!unit) continue;
                if (unit->Strength() >= unit->establishment) continue;

                // A lord's own person is not replaced out of the village.
                if (db.Role(unit->role).single) continue;

                const u32 gap = unit->establishment - unit->Strength();
                u32 intake = static_cast<u32>(std::max(1.0f,
                    static_cast<f32>(unit->establishment) * sharePerDay * static_cast<f32>(days)));
                intake = std::min(intake, gap);
                intake = std::min<u32>(intake, static_cast<u32>(home->population - populationFloor));
                if (intake == 0) continue;

                const f32 cost = static_cast<f32>(intake) * headCost;
                if (clan->resources.money < cost) break;      // nothing more is affordable today

                clan->resources.money -= cost;
                home->population -= static_cast<i32>(intake);

                for (u32 i = 0; i < intake; ++i)
                {
                    Character& person = CharacterFactory::CreateCommoner(world, unit->raceId,
                                                                          home->name, random);
                    unit->characters.push_back(person.id);
                }

                // Raw men dilute a veteran company: the drill of the whole unit slips back
                // towards that of a recruit, in proportion to how many of them there are.
                const f32 share = static_cast<f32>(intake) / static_cast<f32>(unit->Strength());
                unit->training = Clamp01(unit->training * (1.0f - share) + db.BaseTraining() * share);
            }
        }
    }

    void BattleSystem::TickRecovery(World& world, f32 days)
    {
        // Recovery and attrition are counted by the day, not by the frame.
        m_recoveryCarry += days;
        if (m_recoveryCarry < 1.0f) return;

        const f32 elapsed = std::floor(m_recoveryCarry);
        m_recoveryCarry -= elapsed;

        // Gaps in a quartered host are made good out of the town it sits in.
        ReinforceGarrisons(world, static_cast<i32>(elapsed));

        ConfigManager& config = ConfigManager::Get();
        const f32 healPerDay = config.Float("battle/woundedHealPerDay", 0.045f);
        const f32 diePerDay = config.Float("battle/woundedDiePerDay", 0.006f);
        const f32 restBonus = config.Float("battle/woundedRestBonus", 2.0f);

        Random& random = GlobalRandom();

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            // Men mend fastest lying still behind their own walls, slowest on the march.
            f32 rate = healPerDay;
            if (cohort.garrisonOf != kInvalidId) rate *= restBonus;
            else if (cohort.currentTask.IsMoving()) rate *= 0.5f;
            rate *= 0.4f + Clamp01(cohort.supply) * 0.6f;

            for (EntityId unitId : cohort.units)
            {
                Unit* unit = world.FindUnit(unitId);
                if (!unit || unit->wounded.empty()) continue;

                const f32 chanceBack = 1.0f - std::pow(1.0f - Clamp01(rate), elapsed);
                const f32 chanceDead = 1.0f - std::pow(1.0f - Clamp01(diePerDay), elapsed);

                for (size_t i = unit->wounded.size(); i-- > 0;)
                {
                    const EntityId personId = unit->wounded[i];
                    if (random.Chance(chanceDead))
                    {
                        // Died of his wounds. The chronicle does not name him, but the
                        // muster roll is one shorter for it.
                        world.Characters().erase(personId);
                        unit->wounded.erase(unit->wounded.begin() + static_cast<i64>(i));
                    }
                    else if (random.Chance(chanceBack))
                    {
                        unit->characters.push_back(personId);
                        unit->wounded.erase(unit->wounded.begin() + static_cast<i64>(i));
                    }
                }
            }
        }

        ApplyAttrition(world, elapsed);

        // Hosts that were pulling back have finished doing so.
        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.disengageDays > 0.0f) cohort.disengageDays = std::max(0.0f, cohort.disengageDays - elapsed);
        }
    }

    void BattleSystem::ApplyAttrition(World& world, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 threshold = config.Float("battle/attritionSupplyThreshold", 0.55f);
        const f32 rate = config.Float("battle/attritionPerDay", 0.012f);
        if (rate <= 0.0f) return;

        Random& random = GlobalRandom();
        std::vector<EntityId> emptied;

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            // A host that is fed and in order loses nobody. One living off a picked-over
            // countryside bleeds men every day it stays out, and most of them are not dead
            // but simply done in.
            const f32 want = Clamp01(threshold);
            const f32 have = Clamp01(cohort.supply);
            if (have >= want) continue;

            const f32 severity = (want - have) / std::max(0.05f, want);

            // The bigger the host, the worse it fares: more mouths on the same road, more
            // stragglers, and a baggage train that cannot keep up with any of them.
            const f32 head = static_cast<f32>(world.CohortStrength(cohortId));
            const f32 comfortable = config.Float("battle/attritionComfortableSize", 300.0f);
            const f32 crowd = 1.0f + std::max(0.0f, head - comfortable) / comfortable *
                                     config.Float("battle/attritionSizePenalty", 0.45f);

            const f32 daily = rate * severity * crowd * days;

            for (EntityId unitId : cohort.units)
            {
                Unit* unit = world.FindUnit(unitId);
                if (!unit || unit->characters.empty()) continue;

                const f32 expected = daily * static_cast<f32>(unit->characters.size());
                u32 falling = static_cast<u32>(expected);
                if (random.Chance(expected - static_cast<f32>(falling))) ++falling;
                falling = std::min<u32>(falling, static_cast<u32>(unit->characters.size()));

                for (u32 i = 0; i < falling; ++i)
                {
                    const EntityId personId = unit->characters.back();
                    unit->characters.pop_back();
                    // Exhaustion fells more men than it kills.
                    if (random.Chance(config.Float("battle/attritionKillShare", 0.25f)))
                    {
                        world.Characters().erase(personId);
                    }
                    else
                    {
                        unit->wounded.push_back(personId);
                    }
                }

                unit->fatigue = Clamp01(unit->fatigue + daily * 2.0f);
                if (unit->IsGone()) emptied.push_back(unitId);
            }
        }

        for (EntityId unitId : emptied) world.DestroyUnit(unitId);
    }

    void BattleSystem::GatherSupport(World& world, BattleReport& report)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 reach = m_engagementRadius *
                          config.Float("battle/supportRadiusFactor", 2.2f);

        for (auto& [id, cohort] : world.Cohorts())
        {
            if (cohort.IsEmpty() || cohort.IsWithdrawing()) continue;
            if (report.attacker.Includes(id) || report.defender.Includes(id)) continue;
            if (cohort.garrisonOf != kInvalidId) continue;   // garrisons fight sieges, not fields
            if (BattleOf(id)) continue;                      // already committed elsewhere
            if (Distance(cohort.position, report.position) > reach) continue;

            // Which line does it belong in? The one it is not at war with. A host that is
            // hostile to both stays out of it - it has its own quarrel and will get its own
            // battle when somebody closes.
            const bool foeOfAttacker = world.AreHostile(cohort.clan, report.attacker.clan);
            const bool foeOfDefender = world.AreHostile(cohort.clan, report.defender.clan);

            BattleSide* side = nullptr;
            if (foeOfDefender && !foeOfAttacker) side = &report.attacker;
            else if (foeOfAttacker && !foeOfDefender) side = &report.defender;
            if (!side) continue;

            // ...and it must actually be friendly to the side it is joining: a third party
            // at peace with both is a spectator.
            const Clan* mine = world.FindClan(cohort.clan);
            const Clan* theirs = world.FindClan(side->clan);
            if (!mine || !theirs || mine->state != theirs->state) continue;

            side->support.push_back(id);
            side->startingStrength += world.CohortStrength(id);
            cohort.currentTask.Clear();

            world.Log(cohort.DisplayName() + " заходить у бій на підмогу",
                      mine->color);
        }
    }

    void BattleSystem::MeasureFlanks(World& world, BattleReport& report)
    {
        report.attacker.flanked = 0.0f;
        report.defender.flanked = 0.0f;

        ConfigManager& config = ConfigManager::Get();
        const f32 cap = config.Float("battle/flankCap", 0.85f);

        auto measure = [&](const BattleSide& pressing, BattleSide& pressed)
        {
            const Cohort* anchor = world.FindCohort(pressed.cohort);
            const Cohort* first = world.FindCohort(pressing.cohort);
            if (!anchor || !first) return;

            // Where the pressed host is already facing: towards whoever engaged it first.
            const Vec2 front = (first->position - anchor->position).Normalized();
            if (front.LengthSq() < 0.0001f) return;

            f32 total = 0.0f;
            f32 weight = 0.0f;
            for (EntityId id : pressing.support)
            {
                const Cohort* host = world.FindCohort(id);
                if (!host || host->IsEmpty()) continue;

                const Vec2 bearing = (host->position - anchor->position).Normalized();
                if (bearing.LengthSq() < 0.0001f) continue;

                // 1 straight ahead, 0 straight behind. Half of one minus it is therefore
                // nothing for a host beside the first, a half at right angles, and the
                // whole of it for a host at the enemy's back - which is the rule asked for.
                const f32 alignment = front.x * bearing.x + front.y * bearing.y;
                const f32 round = Clamp01((1.0f - alignment) * 0.5f);

                // A handful of men coming round the back is a fright; a second army coming
                // round the back is the end of the battle. Weigh it by what it brings.
                const f32 share = static_cast<f32>(world.CohortStrength(id));
                total += round * share;
                weight += share;
            }
            if (weight <= 0.0f) return;

            // How much of the pressed side's own strength the encircling force amounts to,
            // so that being taken in the rear by something small is not decisive.
            const f32 held = static_cast<f32>(SideStrength(world, pressed));
            const f32 mass = held > 0.0f ? std::min(1.0f, weight / held) : 1.0f;
            pressed.flanked = std::min(cap, total / weight * mass);
        };

        measure(report.attacker, report.defender);
        measure(report.defender, report.attacker);
    }

    void BattleSystem::ResolveFieldBattles(World& world, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();
        m_engagementRadius = config.Float("battle/engagementRadius", 18.0f);
        const f32 roundsPerDay = config.Float("battle/roundsPerDay", 4.0f);

        for (auto& [id, cohort] : world.Cohorts()) cohort.inBattle = false;

        // --- battles already under way ---------------------------------------------------
        // A fight is a standing thing, not something resolved the moment two banners meet.
        // Each one carries its own fractional round count, so its pace follows the game
        // clock and not the frame rate - at half speed a battle takes twice as long, and
        // at sixty frames a second it does not burn sixty rounds.
        std::vector<BattleReport> carried;
        carried.reserve(m_active.size());

        for (BattleReport& battle : m_active)
        {
            Cohort* a = world.FindCohort(battle.attacker.cohort);
            Cohort* b = world.FindCohort(battle.defender.cohort);
            if (!a || !b || a->IsEmpty() || b->IsEmpty()) continue;
            if (!world.AreHostile(a->clan, b->clan)) continue;            // peace broke out
            if (a->IsWithdrawing() || b->IsWithdrawing()) continue;       // one of them is pulling out
            if (Distance(a->position, b->position) > m_engagementRadius * 1.5f) continue;

            battle.position = (a->position + b->position) * 0.5f;

            // Hosts that have lost their men, or that have walked off, are no longer in it.
            auto prune = [&](BattleSide& side)
            {
                side.support.erase(std::remove_if(side.support.begin(), side.support.end(),
                    [&](EntityId id)
                    {
                        const Cohort* host = world.FindCohort(id);
                        return !host || host->IsEmpty() || host->IsWithdrawing() ||
                               Distance(host->position, battle.position) >
                                   m_engagementRadius * config.Float("battle/supportRadiusFactor", 2.2f);
                    }), side.support.end());
            };
            prune(battle.attacker);
            prune(battle.defender);

            GatherSupport(world, battle);
            MeasureFlanks(world, battle);

            for (EntityId id : battle.attacker.Hosts())
            {
                if (Cohort* host = world.FindCohort(id)) host->inBattle = true;
            }
            for (EntityId id : battle.defender.Hosts())
            {
                if (Cohort* host = world.FindCohort(id)) host->inBattle = true;
            }

            battle.elapsedDays += days;
            battle.roundProgress += roundsPerDay * days;
            const i32 rounds = static_cast<i32>(battle.roundProgress);
            battle.roundProgress -= static_cast<f32>(rounds);

            if (rounds > 0) RunRounds(world, battle, rounds);
            if (battle.concluded)
            {
                // A relief force that turns up mid-siege interrupts the siege; beating it
                // off should put the besiegers back where they were, and not leave them
                // standing outside the walls waiting for an order they already gave.
                auto release = [&](const BattleSide& side)
                {
                    for (EntityId id : side.Hosts())
                    {
                        Cohort* host = world.FindCohort(id);
                        if (!host) continue;
                        host->inBattle = false;

                        if (host->siegeTarget == kInvalidId) continue;
                        const Settlement* wall = world.FindSettlement(host->siegeTarget);
                        if (!wall || host->currentTask.type != TaskType::Idle ||
                            Distance(host->position, wall->position) > 40.0f ||
                            !world.MayAttackSettlement(host->clan, wall->id))
                        {
                            host->siegeTarget = kInvalidId;
                            continue;
                        }
                        host->currentTask.Clear();
                        host->currentTask.type = TaskType::Besiege;
                        host->currentTask.destination = host->position;
                        host->currentTask.targetSettlement = wall->id;
                    }
                };
                release(battle.attacker);
                release(battle.defender);
                continue;
            }
            carried.push_back(battle);
        }

        // --- fresh contacts ----------------------------------------------------------------
        // Each cohort fights at most one battle: a host that is already in a line, on either
        // side of it, is not available to start another one somewhere else.
        std::vector<EntityId> ids;
        ids.reserve(world.Cohorts().size());
        for (const auto& [id, cohort] : world.Cohorts()) ids.push_back(id);
        std::sort(ids.begin(), ids.end());

        auto alreadyFighting = [&carried](EntityId id)
        {
            for (const BattleReport& battle : carried)
            {
                if (battle.attacker.Includes(id) || battle.defender.Includes(id)) return true;
            }
            return false;
        };

        for (size_t i = 0; i < ids.size(); ++i)
        {
            Cohort* a = world.FindCohort(ids[i]);
            if (!a || a->IsEmpty() || a->IsWithdrawing() || alreadyFighting(ids[i])) continue;

            for (size_t j = i + 1; j < ids.size(); ++j)
            {
                Cohort* b = world.FindCohort(ids[j]);
                if (!b || b->IsEmpty() || b->IsWithdrawing() || alreadyFighting(ids[j])) continue;
                if (!world.AreHostile(a->clan, b->clan)) continue;
                if (Distance(a->position, b->position) > m_engagementRadius) continue;

                a->inBattle = b->inBattle = true;

                BattleReport report;
                report.day = world.Time().TotalDays();
                report.position = (a->position + b->position) * 0.5f;
                report.terrainName = world.Map().TerrainAtMap(report.position).name;

                report.attacker.cohort = a->id;
                report.attacker.clan = a->clan;
                report.attacker.name = a->DisplayName();
                report.attacker.startingStrength = world.CohortStrength(a->id);

                report.defender.cohort = b->id;
                report.defender.clan = b->clan;
                report.defender.name = b->DisplayName();
                report.defender.startingStrength = world.CohortStrength(b->id);

                const Clan* clan = world.FindClan(a->clan);
                world.Log("Бій при " + report.terrainName + ": " + report.attacker.name +
                          " проти " + report.defender.name,
                          clan ? clan->color : Color::FromRGB(0xC9A227));

                // Anyone already standing near enough goes straight into the line.
                GatherSupport(world, report);
                MeasureFlanks(world, report);
                for (EntityId id : report.attacker.Hosts())
                {
                    if (Cohort* host = world.FindCohort(id)) host->inBattle = true;
                }
                for (EntityId id : report.defender.Hosts())
                {
                    if (Cohort* host = world.FindCohort(id)) host->inBattle = true;
                }

                report.roundProgress = roundsPerDay * days;
                const i32 rounds = static_cast<i32>(report.roundProgress);
                report.roundProgress -= static_cast<f32>(rounds);
                report.elapsedDays = days;
                if (rounds > 0) RunRounds(world, report, rounds);

                if (!report.concluded) carried.push_back(report);
                else
                {
                    for (EntityId id : report.attacker.Hosts())
                    {
                        if (Cohort* host = world.FindCohort(id)) host->inBattle = false;
                    }
                    for (EntityId id : report.defender.Hosts())
                    {
                        if (Cohort* host = world.FindCohort(id)) host->inBattle = false;
                    }
                }
                break;
            }
        }

        m_active.swap(carried);
    }

    void BattleSystem::RunRounds(World& world, BattleReport& report, i32 rounds)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 baseDamage = config.Float("battle/baseDamage", 0.06f);
        const f32 breakThreshold = config.Float("battle/moraleBreakThreshold", 0.22f);
        const f32 moraleLoss = config.Float("battle/moraleLossPerCasualty", 0.9f);
        const f32 experienceGain = config.Float("battle/experiencePerRound", 0.004f);
        const f32 experienceCap = config.Float("battle/experienceCap", 1.0f);
        const f32 flankMorale = config.Float("battle/flankMoralePerRound", 0.05f);
        const f32 flankPower = config.Float("battle/flankPowerPenalty", 0.35f);

        Random& random = GlobalRandom();

        // Every living host on a side, so the round can reach the whole line at once.
        auto forEachUnit = [&world](const BattleSide& side, auto&& fn)
        {
            for (EntityId cohortId : side.Hosts())
            {
                Cohort* host = world.FindCohort(cohortId);
                if (!host) continue;
                for (EntityId unitId : host->units)
                {
                    if (Unit* unit = world.FindUnit(unitId)) fn(*host, *unit);
                }
            }
        };

        for (i32 round = 0; round < rounds; ++round)
        {
            Cohort* a = world.FindCohort(report.attacker.cohort);
            Cohort* b = world.FindCohort(report.defender.cohort);
            if (!a || !b || a->IsEmpty() || b->IsEmpty()) break;

            f32 powerA = SidePower(world, report.attacker, report.defender);
            f32 powerB = SidePower(world, report.defender, report.attacker);

            // A line that has to face two ways is not fighting with all of itself. This is
            // the part of being flanked that is felt in the blows rather than in the nerve.
            powerA *= 1.0f - report.attacker.flanked * flankPower;
            powerB *= 1.0f - report.defender.flanked * flankPower;

            report.attacker.power = powerA;
            report.defender.power = powerB;
            if (powerA <= 0.0f && powerB <= 0.0f) break;

            // Damage is exchanged simultaneously, with a little luck on each side.
            f32 damageToB = powerA * baseDamage * random.RangeF(0.85f, 1.15f);
            f32 damageToA = powerB * baseDamage * random.RangeF(0.85f, 1.15f);

            // Nobody walks off a field untouched. However lopsided the odds, men with
            // spears in their hands take some toll simply by being there, so each side
            // deals at least a scratch proportional to how many of them are swinging.
            const f32 bite = config.Float("battle/minBitePerHead", 0.9f);
            damageToA = std::max(damageToA, static_cast<f32>(SideStrength(world, report.defender)) * bite);
            damageToB = std::max(damageToB, static_cast<f32>(SideStrength(world, report.attacker)) * bite);

            // Most of what a line of battle takes is men down, not men dead: a shield wall
            // wounds far more than it kills. The killing happens in the pursuit.
            const f32 killShare = config.Float("battle/meleeKillShare", 0.4f);
            u32 deadA = 0, deadB = 0, hurtA = 0, hurtB = 0;
            SpreadDamage(world, report.defender, damageToB, deadB, hurtB, killShare);
            SpreadDamage(world, report.attacker, damageToA, deadA, hurtA, killShare);
            report.attacker.losses += deadA;
            report.defender.losses += deadB;
            report.attacker.hurt += hurtA;
            report.defender.hurt += hurtB;
            const u32 lossesA = deadA + hurtA;
            const u32 lossesB = deadB + hurtB;

            // Morale erodes with casualties, and faster still when the blows are coming
            // from behind: it is being taken in the rear that actually breaks armies.
            auto shakeMorale = [&](const BattleSide& side, u32 losses)
            {
                const u32 startingStrength = side.startingStrength;
                const f32 fraction = startingStrength == 0
                    ? 0.0f
                    : static_cast<f32>(losses) / static_cast<f32>(startingStrength);
                const f32 drop = fraction * moraleLoss + side.flanked * flankMorale;
                if (drop <= 0.0f) return;
                forEachUnit(side, [&](Cohort&, Unit& unit)
                {
                    unit.morale = Clamp01(unit.morale - drop);
                });
            };
            shakeMorale(report.attacker, lossesA);
            shakeMorale(report.defender, lossesB);

            // Every round of melee costs order, and everybody in the line learns from it.
            const f32 shock = config.Float("battle/organisationPerRound", 0.06f);
            for (const BattleSide* side : { &report.attacker, &report.defender })
            {
                for (EntityId id : side->Hosts())
                {
                    Cohort* host = world.FindCohort(id);
                    if (!host) continue;
                    host->experience = std::min(experienceCap, host->experience + experienceGain);
                    host->organisation = Clamp01(host->organisation - shock);
                }
            }

            auto averageMorale = [&](const BattleSide& side)
            {
                f32 total = 0.0f;
                u32 count = 0;
                forEachUnit(side, [&](Cohort&, Unit& unit)
                {
                    if (unit.IsDestroyed()) return;
                    total += unit.morale;
                    ++count;
                });
                return count > 0 ? total / static_cast<f32>(count) : 0.0f;
            };

            CullBrokenHosts(world, report);

            const f32 moraleA = averageMorale(report.attacker);
            const f32 moraleB = averageMorale(report.defender);

            if (moraleA < breakThreshold || moraleB < breakThreshold)
            {
                const bool attackerBroke = moraleA <= moraleB;
                report.attacker.routed = attackerBroke;
                report.defender.routed = !attackerBroke;
                report.victor = attackerBroke ? b->clan : a->clan;
                report.concluded = true;

                BattleSide& losingSide = attackerBroke ? report.attacker : report.defender;
                BattleSide& winningSide = attackerBroke ? report.defender : report.attacker;
                Cohort* loser = attackerBroke ? a : b;
                Cohort* winner = attackerBroke ? b : a;
                const Vec2 winnerPosition = winner->position;

                // The pursuit is where the dying is done - but a broken host is not an
                // annihilated one. It runs, loses its order, and can be rallied.
                u32 pursuitDead = 0, pursuitHurt = 0;
                SpreadDamage(world, losingSide,
                             SidePower(world, winningSide, losingSide) *
                             baseDamage * config.Float("battle/pursuitDamage", 1.5f),
                             pursuitDead, pursuitHurt,
                             config.Float("battle/pursuitKillShare", 0.7f));
                losingSide.losses += pursuitDead;
                losingSide.hurt += pursuitHurt;

                // A rearguard that turns and fights, and the horses that founder in the
                // chase: running a broken army down is not free either.
                u32 chaseDead = 0, chaseHurt = 0;
                SpreadDamage(world, winningSide,
                             static_cast<f32>(SideStrength(world, losingSide)) *
                             config.Float("battle/pursuitCostPerHead", 0.5f),
                             chaseDead, chaseHurt, config.Float("battle/meleeKillShare", 0.4f));
                winningSide.losses += chaseDead;
                winningSide.hurt += chaseHurt;

                // How one-sided it was. The odds at the moment of breaking, how far round the
                // beaten line the enemy had got, and whether the victors ride faster than the
                // beaten can run: a rout before horsemen is a massacre, one before foot is not.
                const f32 winnerPower = std::max(0.0f, attackerBroke ? powerB : powerA);
                const f32 loserPower = std::max(1.0f, attackerBroke ? powerA : powerB);
                f32 slowestLoser = 1e9f, slowestWinner = 1e9f;
                for (EntityId id : losingSide.Hosts())
                    slowestLoser = std::min(slowestLoser, MovementSystem::Get().CohortSpeed(world, id));
                for (EntityId id : winningSide.Hosts())
                    slowestWinner = std::min(slowestWinner, MovementSystem::Get().CohortSpeed(world, id));
                const f32 chase = slowestLoser > 0.0f && slowestLoser < 1e8f && slowestWinner < 1e8f
                    ? std::clamp(slowestWinner / slowestLoser, 0.75f, 1.6f) : 1.0f;
                const f32 decisiveness = winnerPower / loserPower * (1.0f + losingSide.flanked) * chase;

                // The whole beaten line gives way, not only the banner that started it.
                const f32 routDistance = config.Float("battle/routDistance", 60.0f);
                const f32 routOrganisation = config.Float("battle/routOrganisation", 0.35f);
                const f32 disengage = config.Float("battle/disengageDays", 3.0f);
                const std::vector<EntityId> breaking = losingSide.Hosts();
                for (EntityId id : breaking)
                {
                    Cohort* host = world.FindCohort(id);
                    if (!host) continue;

                    if (IsAnnihilated(world, *host, host->organisation, decisiveness))
                    {
                        const Clan* beaten = world.FindClan(host->clan);
                        world.Log(host->DisplayName() + ": розгромлено вщент, ніхто не врятувався",
                                  beaten ? beaten->color : Color::FromRGB(0xB0413E));
                        losingSide.losses += world.CohortStrength(id);
                        world.DestroyCohort(id);
                        continue;
                    }
                    host->currentTask.Clear();
                    host->organisation = Clamp01(host->organisation * routOrganisation);
                    host->disengageDays = disengage;
                    host->inBattle = false;
                    // Onto ground, and away from the man who beat them - or nowhere, and
                    // then the field is where they stay.
                    FallBack(world, *host, winnerPosition, routDistance);
                }
                for (EntityId id : winningSide.Hosts())
                {
                    if (Cohort* host = world.FindCohort(id)) host->inBattle = false;
                }
                (void)loser;
                break;
            }
        }

        Cohort* a = world.FindCohort(report.attacker.cohort);
        Cohort* b = world.FindCohort(report.defender.cohort);
        const bool attackerDead = !a || a->IsEmpty();
        const bool defenderDead = !b || b->IsEmpty();

        if (attackerDead || defenderDead)
        {
            report.concluded = true;
            report.victor = attackerDead ? report.defender.clan : report.attacker.clan;
        }

        if (report.concluded)
        {
            // No battle is won for nothing. If the chances happened to spare the winner
            // entirely - a brief skirmish against a host that broke at once - he still
            // carries somebody back to camp. A victory in the chronicle with no cost
            // beside it reads as a rounding error, and it is one.
            auto exactToll = [&](BattleSide& side)
            {
                if (side.losses + side.hurt > 0) return;
                for (EntityId cohortId : side.Hosts())
                {
                    Cohort* host = world.FindCohort(cohortId);
                    if (!host || host->IsEmpty()) continue;

                    for (EntityId unitId : host->units)
                    {
                        Unit* unit = world.FindUnit(unitId);
                        if (!unit || unit->characters.empty()) continue;

                        const EntityId personId = unit->characters.back();
                        unit->characters.pop_back();
                        unit->wounded.push_back(personId);
                        ++side.hurt;
                        return;
                    }
                }
            };
            if (report.victor == report.attacker.clan) exactToll(report.attacker);
            else if (report.victor == report.defender.clan) exactToll(report.defender);

            const Clan* victor = world.FindClan(report.victor);
            const std::string where = report.terrainName;
            auto toll = [](const BattleSide& side)
            {
                std::string text = std::to_string(side.losses) + " полеглих, " +
                                   std::to_string(side.hurt) + " поранених";
                if (!side.support.empty())
                {
                    text += ", загонів: " + std::to_string(side.support.size() + 1);
                }
                return text;
            };
            world.Log("Битва при " + where + " (" +
                      std::to_string(static_cast<i32>(report.elapsedDays + 0.5f)) + " дн.): " +
                      report.attacker.name + " (" + toll(report.attacker) + ") проти " +
                      report.defender.name + " (" + toll(report.defender) +
                      "). Перемога: " + (victor ? victor->name : std::string("ніхто")),
                      victor ? victor->color : Color::FromRGB(0x8B97A4));

            m_history.push_back(report);
            if (m_history.size() > 80) m_history.erase(m_history.begin());

            if (attackerDead) world.DestroyCohort(report.attacker.cohort);
            if (defenderDead) world.DestroyCohort(report.defender.cohort);
        }
    }

    void BattleSystem::ApplyDamage(World& world, Cohort& target, f32 damage, u32& deadOut,
                                   u32& hurtOut, f32 killShare)
    {
        if (damage <= 0.0f) return;
        Random& random = GlobalRandom();

        // Spread the blow across the line in proportion to how much of it each unit holds.
        f32 totalDefense = 0.0f;
        for (EntityId unitId : target.units)
        {
            const Unit* unit = world.FindUnit(unitId);
            if (!unit || unit->IsDestroyed()) continue;
            totalDefense += unit->DefensivePower(target.experience);
        }
        if (totalDefense <= 0.0f) return;

        std::vector<EntityId> emptied;
        for (EntityId unitId : target.units)
        {
            Unit* unit = world.FindUnit(unitId);
            if (!unit || unit->IsDestroyed()) continue;

            const f32 share = unit->DefensivePower(target.experience) / totalDefense;
            const f32 unitDamage = damage * share;

            // Convert damage into whole casualties using the unit's staying power. The
            // remainder is settled by chance rather than thrown away: truncating it meant a
            // blow spread thin across ten units came to nothing at all in every one of
            // them, which is how an army could win a battle without losing a man.
            const f32 perHead = std::max(1.0f, unit->Stats().health * (1.0f + unit->Stats().defense * 0.08f));
            const f32 exact = unitDamage / perHead;
            u32 casualties = static_cast<u32>(exact);
            if (random.Chance(exact - static_cast<f32>(casualties))) ++casualties;
            casualties = std::min<u32>(casualties, unit->Strength());
            if (casualties == 0) continue;

            for (u32 i = 0; i < casualties; ++i)
            {
                const EntityId characterId = unit->characters.back();
                unit->characters.pop_back();

                if (random.Chance(Clamp01(killShare)))
                {
                    world.Characters().erase(characterId);
                    ++deadOut;
                }
                else
                {
                    // Carried off. He is the same man, and he will be back.
                    unit->wounded.push_back(characterId);
                    ++hurtOut;
                }
            }
            if (unit->IsGone()) emptied.push_back(unitId);
        }

        for (EntityId unitId : emptied) world.DestroyUnit(unitId);
    }

    void BattleSystem::ResolveSieges(World& world, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();

        // Clear last tick's siege markers; they are re-established below.
        for (auto& [id, settlement] : world.Settlements()) settlement.besiegedBy = kInvalidId;

        std::vector<std::pair<EntityId, EntityId>> captures;   // settlement, new owner

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.currentTask.type != TaskType::Besiege) continue;
            if (cohort.currentTask.IsMoving()) continue;

            Settlement* settlement = world.FindSettlement(cohort.currentTask.targetSettlement);
            if (!settlement) { cohort.currentTask.Clear(); continue; }
            if (Distance(cohort.position, settlement->position) > 40.0f) continue;
            // A siege happens in war and nowhere else; an unclaimed holding is fair game.
            if (!world.MayAttackSettlement(cohort.clan, settlement->id))
            {
                cohort.currentTask.Clear();
                continue;
            }

            settlement->besiegedBy = cohort.clan;

            const f32 attack = world.CohortPower(cohortId) * 0.01f;
            const f32 defense = SettlementDefense(world, settlement->id);

            // Is there anybody actually holding the place? An open village with nobody in
            // it is not besieged, it is ridden into: the reeve hands over the keys and the
            // column is on its way the same afternoon. Only walls and men behind them turn
            // the business into a siege.
            bool held = false;
            for (const auto& [otherId, other] : world.Cohorts())
            {
                if (other.garrisonOf == settlement->id && !other.IsEmpty()) { held = true; break; }
            }
            const bool walled = settlement->HasBuilding("palisade") || settlement->HasBuilding("stoneWall");
            const bool open = settlement->kind == SettlementKind::Village && !held && !walled;

            if (defense <= 0.0f) { settlement->siegeProgress = 1.0f; }
            else
            {
                // A siege is a slow grind: superiority sets the pace, it does not skip the
                // work. How much work there is depends on what is being stormed - a village
                // has a ditch and a fence, a stone castle has walls worth the name.
                const char* pace = open                                        ? "battle/siegePace/openVillage"
                                 : settlement->kind == SettlementKind::Village ? "battle/siegePace/village"
                                 : settlement->kind == SettlementKind::City    ? "battle/siegePace/city"
                                                                               : "battle/siegePace/castle";
                const f32 ratio = attack / (attack + defense);
                settlement->siegeProgress += ratio * config.Float(pace, 0.05f) * days;
            }

            // Starving the walls also costs the besiegers.
            for (EntityId unitId : cohort.units)
            {
                if (Unit* unit = world.FindUnit(unitId))
                {
                    unit->fatigue = Clamp01(unit->fatigue + 0.005f * days);
                }
            }

            // And so does taking them. Even a village with a fence and a few angry men on
            // it costs the besieger somebody; a stone castle costs him a great many. Most
            // of it is men carried back to the camp rather than men buried.
            const char* costKey = settlement->kind == SettlementKind::Village ? "battle/siegeCost/village"
                                : settlement->kind == SettlementKind::City    ? "battle/siegeCost/city"
                                                                              : "battle/siegeCost/castle";
            const f32 bleed = defense * config.Float(costKey, 0.35f) * days * (open ? 0.25f : 1.0f);
            if (bleed > 0.0f)
            {
                u32 dead = 0, hurt = 0;
                ApplyDamage(world, cohort, bleed, dead, hurt,
                            config.Float("battle/siegeKillShare", 0.35f));
            }

            if (settlement->siegeProgress >= 1.0f)
            {
                captures.emplace_back(settlement->id, cohort.clan);
                cohort.currentTask.Clear();
                cohort.currentTask.type = TaskType::Garrison;
                cohort.currentTask.targetSettlement = settlement->id;
                cohort.garrisonOf = settlement->id;
            }
        }

        // Several armies can finish a siege on the same tick; only the first one takes the
        // place, the rest simply find the gates already open.
        std::vector<EntityId> taken;
        for (const auto& [settlementId, newOwner] : captures)
        {
            if (std::find(taken.begin(), taken.end(), settlementId) != taken.end()) continue;
            taken.push_back(settlementId);
            CaptureSettlement(world, settlementId, newOwner);
        }

        // Sieges that were lifted recover.
        for (auto& [id, settlement] : world.Settlements())
        {
            if (settlement.besiegedBy == kInvalidId && settlement.siegeProgress > 0.0f)
            {
                settlement.siegeProgress = std::max(0.0f, settlement.siegeProgress - 0.04f * days);
            }
        }
    }

    void BattleSystem::CaptureSettlement(World& world, EntityId settlementId, EntityId newOwner)
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        Clan* conqueror = world.FindClan(newOwner);
        if (!settlement || !conqueror) return;

        ConfigManager& config = ConfigManager::Get();

        if (Clan* previous = world.FindClan(settlement->owner))
        {
            previous->RemoveSettlement(settlementId);
        }

        // The men who held the walls held them to the end. Whatever garrison was inside -
        // the old lord's, or the villagers who had risen and were keeping the place for
        // themselves - does not survive the storming and does not carry on sitting there
        // under the new banner.
        std::vector<EntityId> fallen;
        for (const auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.garrisonOf != settlementId) continue;
            if (cohort.clan == newOwner) continue;
            fallen.push_back(cohortId);
        }
        for (EntityId cohortId : fallen)
        {
            const Cohort* garrison = world.FindCohort(cohortId);
            const std::string name = garrison ? garrison->DisplayName() : std::string();
            world.DestroyCohort(cohortId);
            if (!name.empty())
            {
                world.Log(name + " полягла, боронячи " + settlement->name,
                          Color::FromRGB(0xC05046));
            }
        }

        // The victors take what they can carry.
        const f32 loot = config.Float("battle/lootFraction", 0.35f);
        conqueror->resources.money += static_cast<f32>(settlement->population) * 0.15f * loot;
        conqueror->resources.food += static_cast<f32>(settlement->population) * 0.05f * loot;

        // Where the country around it answers to nobody, the place is held by presence and
        // by nothing else: should a realm's authority close over it later, it changes hands
        // without a blow. Taken on one's own ground, or off an enemy inside his own borders,
        // it is a possession like any other.
        const EntityId ground = CoverageSystem::Get().OwnerAt(world, settlement->position);
        settlement->heldByPresence = ground == kInvalidId;

        settlement->owner = newOwner;
        settlement->siegeProgress = 0.0f;
        settlement->besiegedBy = kInvalidId;
        // A town takes a while to get used to a new lord, whoever he is.
        settlement->newLordUntilDay = world.Time().TotalDays() +
            ConfigManager::Get().Int("population/newLordDays", 240);
        settlement->loyalty = std::max(0.12f, settlement->loyalty * 0.4f);
        settlement->prosperity *= 1.0f - config.Float("battle/razeProsperityLoss", 0.6f) * 0.5f;
        settlement->population = std::max(30, static_cast<i32>(settlement->population * 0.88f));
        conqueror->AddSettlement(settlementId);

        // A castle is taken with the country it holds. Every village that answered to this
        // seat now answers to whoever holds it - the peasants did not fight and are not
        // asked; they merely find a different lord's men at the gate.
        if (settlement->kind != SettlementKind::Village)
        {
            for (EntityId subjectId : CoverageSystem::Get().SubjectsOf(world, settlementId))
            {
                Settlement* village = world.FindSettlement(subjectId);
                if (!village || village->id == settlementId) continue;
                if (village->kind != SettlementKind::Village) continue;
                if (village->owner == newOwner) continue;

                if (Clan* lord = world.FindClan(village->owner)) lord->RemoveSettlement(subjectId);
                village->owner = newOwner;
                conqueror->AddSettlement(subjectId);

                // Changing masters at swordpoint is not popular.
                village->loyalty = std::max(0.15f, village->loyalty * 0.6f);
                world.Log(village->name + " переходить до роду " + conqueror->name, conqueror->color);
            }
        }

        world.Log(settlement->name + " взято родом " + conqueror->name, conqueror->color);
        CoverageSystem::Get().MarkDirty();
    }

    void BattleSystem::ResolveRaids(World& world)
    {
        const Json& action = SettlementDatabase::Get().Action("raid");

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.currentTask.type != TaskType::Raid) continue;
            if (cohort.currentTask.IsMoving()) continue;

            Settlement* settlement = world.FindSettlement(cohort.currentTask.targetSettlement);
            if (!settlement) { cohort.currentTask.Clear(); continue; }
            if (Distance(cohort.position, settlement->position) > 40.0f) continue;

            // Plunder needs both a war and an army willing to do it.
            if (!cohort.mayRaid || !world.MayAttackSettlement(cohort.clan, settlement->id))
            {
                cohort.currentTask.Clear();
                continue;
            }

            Clan* raider = world.FindClan(cohort.clan);
            if (!raider) { cohort.currentTask.Clear(); continue; }

            const f32 population = static_cast<f32>(settlement->population);
            raider->resources.money += population * action["lootMoneyPerPopulation"].AsFloat(0.12f);
            raider->resources.food += population * action["lootFoodPerPopulation"].AsFloat(0.05f);

            // What was taken from the byres and the grain pits goes into the army's own
            // carts. A hamlet barely tops it up; a rich slobodá can victual a host for a
            // good while. This is the practical reason armies raided at all.
            const f32 perHead = action["supplyPerPopulation"].AsFloat(0.00045f);
            const f32 gained = std::min(population * perHead, action["supplyCap"].AsFloat(0.35f));
            cohort.supply = Clamp01(cohort.supply + gained);

            settlement->prosperity *= 1.0f - Clamp01(action["prosperityLoss"].AsFloat(0.35f));
            settlement->loyalty = std::max(0.0f, settlement->loyalty - action["loyaltyLoss"].AsFloat(0.2f));
            settlement->population = std::max(20, static_cast<i32>(
                population * (1.0f - action["populationLoss"].AsFloat(0.1f))));

            settlement->raidedDay = world.Time().TotalDays();
            world.Log(settlement->name + " пограбовано родом " + raider->name, Color::FromRGB(0xC05046));
            cohort.currentTask.Clear();
        }
    }
}
