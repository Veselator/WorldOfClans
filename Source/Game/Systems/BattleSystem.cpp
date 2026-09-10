#include "BattleSystem.h"
#include "CoverageSystem.h"
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
    }

    void BattleSystem::ResolveFieldBattles(World& world, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();
        m_engagementRadius = 18.0f;

        for (auto& [id, cohort] : world.Cohorts()) cohort.inBattle = false;
        m_active.clear();

        // Pair up hostile cohorts standing in contact. Each cohort fights at most one battle
        // per tick, which keeps a melee of many armies from resolving in an arbitrary order.
        std::vector<EntityId> ids;
        ids.reserve(world.Cohorts().size());
        for (const auto& [id, cohort] : world.Cohorts()) ids.push_back(id);
        std::sort(ids.begin(), ids.end());

        std::vector<u8> engaged(ids.size(), 0);
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (engaged[i]) continue;
            Cohort* a = world.FindCohort(ids[i]);
            if (!a || a->IsEmpty()) continue;

            for (size_t j = i + 1; j < ids.size(); ++j)
            {
                if (engaged[j]) continue;
                Cohort* b = world.FindCohort(ids[j]);
                if (!b || b->IsEmpty()) continue;
                if (!world.AreHostile(a->clan, b->clan)) continue;
                if (Distance(a->position, b->position) > m_engagementRadius) continue;

                engaged[i] = engaged[j] = 1;
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

                const i32 rounds = std::max(1, static_cast<i32>(
                    config.Float("battle/roundsPerDay", 4.0f) * days));
                RunRounds(world, report, rounds);
                m_active.push_back(report);
                break;
            }
        }
    }

    void BattleSystem::RunRounds(World& world, BattleReport& report, i32 rounds)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 baseDamage = config.Float("battle/baseDamage", 0.06f);
        const f32 breakThreshold = config.Float("battle/moraleBreakThreshold", 0.22f);
        const f32 moraleLoss = config.Float("battle/moraleLossPerCasualty", 0.9f);
        const f32 experienceGain = config.Float("battle/experiencePerRound", 0.004f);
        const f32 experienceCap = config.Float("battle/experienceCap", 1.0f);

        Random& random = GlobalRandom();

        for (i32 round = 0; round < rounds; ++round)
        {
            Cohort* a = world.FindCohort(report.attacker.cohort);
            Cohort* b = world.FindCohort(report.defender.cohort);
            if (!a || !b || a->IsEmpty() || b->IsEmpty()) break;

            const f32 powerA = EvaluatePower(world, a->id, b->id, a->position);
            const f32 powerB = EvaluatePower(world, b->id, a->id, b->position);
            report.attacker.power = powerA;
            report.defender.power = powerB;
            if (powerA <= 0.0f && powerB <= 0.0f) break;

            // Damage is exchanged simultaneously, with a little luck on each side.
            const f32 damageToB = powerA * baseDamage * random.RangeF(0.85f, 1.15f);
            const f32 damageToA = powerB * baseDamage * random.RangeF(0.85f, 1.15f);

            u32 lossesA = 0, lossesB = 0;
            ApplyDamage(world, *b, damageToB, lossesB);
            ApplyDamage(world, *a, damageToA, lossesA);
            report.attacker.losses += lossesA;
            report.defender.losses += lossesB;

            // Morale erodes with casualties; the side that breaks first loses the field.
            auto shakeMorale = [&](Cohort& cohort, u32 losses, u32 startingStrength)
            {
                if (startingStrength == 0) return;
                const f32 fraction = static_cast<f32>(losses) / static_cast<f32>(startingStrength);
                for (EntityId unitId : cohort.units)
                {
                    if (Unit* unit = world.FindUnit(unitId))
                    {
                        unit->morale = Clamp01(unit->morale - fraction * moraleLoss);
                    }
                }
            };
            shakeMorale(*a, lossesA, report.attacker.startingStrength);
            shakeMorale(*b, lossesB, report.defender.startingStrength);

            a->experience = std::min(experienceCap, a->experience + experienceGain);
            b->experience = std::min(experienceCap, b->experience + experienceGain);

            auto averageMorale = [&world](const Cohort& cohort)
            {
                f32 total = 0.0f;
                u32 count = 0;
                for (EntityId unitId : cohort.units)
                {
                    const Unit* unit = world.FindUnit(unitId);
                    if (!unit || unit->IsDestroyed()) continue;
                    total += unit->morale;
                    ++count;
                }
                return count > 0 ? total / static_cast<f32>(count) : 0.0f;
            };

            const f32 moraleA = averageMorale(*a);
            const f32 moraleB = averageMorale(*b);

            if (moraleA < breakThreshold || moraleB < breakThreshold)
            {
                const bool attackerBroke = moraleA <= moraleB;
                report.attacker.routed = attackerBroke;
                report.defender.routed = !attackerBroke;
                report.victor = attackerBroke ? b->clan : a->clan;
                report.concluded = true;

                // The broken side runs; the pursuit costs it more than the fighting did.
                Cohort* loser = attackerBroke ? a : b;
                Cohort* winner = attackerBroke ? b : a;
                u32 pursuitLosses = 0;
                ApplyDamage(world, *loser, EvaluatePower(world, winner->id, loser->id, winner->position) *
                                           baseDamage * 1.5f, pursuitLosses);

                const Vec2 away = (loser->position - winner->position).Normalized();
                loser->position += away * 40.0f;
                loser->currentTask.Clear();
                loser->inBattle = false;
                winner->inBattle = false;
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
            const Clan* victor = world.FindClan(report.victor);
            const std::string where = report.terrainName;
            world.Log("Битва при " + where + ": " + report.attacker.name + " (" +
                      std::to_string(report.attacker.losses) + " полеглих) проти " +
                      report.defender.name + " (" + std::to_string(report.defender.losses) +
                      "). Перемога: " + (victor ? victor->name : std::string("ніхто")),
                      victor ? victor->color : Color::FromRGB(0x8B97A4));

            m_history.push_back(report);
            if (m_history.size() > 80) m_history.erase(m_history.begin());

            if (attackerDead) world.DestroyCohort(report.attacker.cohort);
            if (defenderDead) world.DestroyCohort(report.defender.cohort);
        }
    }

    void BattleSystem::ApplyDamage(World& world, Cohort& target, f32 damage, u32& lossesOut)
    {
        if (damage <= 0.0f) return;

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

            // Convert damage into whole casualties using the unit's staying power.
            const f32 perHead = std::max(1.0f, unit->Stats().health * (1.0f + unit->Stats().defense * 0.08f));
            u32 casualties = static_cast<u32>(unitDamage / perHead);
            casualties = std::min<u32>(casualties, unit->Strength());
            if (casualties == 0) continue;

            for (u32 i = 0; i < casualties; ++i)
            {
                const EntityId characterId = unit->characters.back();
                unit->characters.pop_back();
                world.Characters().erase(characterId);
            }
            lossesOut += casualties;
            if (unit->IsDestroyed()) emptied.push_back(unitId);
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
            // Independent holdings may be stormed by anyone; owned ones only in open war.
            if (settlement->owner == cohort.clan) continue;
            if (settlement->owner != kInvalidId && !world.AreHostile(cohort.clan, settlement->owner)) continue;

            settlement->besiegedBy = cohort.clan;

            const f32 attack = world.CohortPower(cohortId) * 0.01f;
            const f32 defense = SettlementDefense(world, settlement->id);
            if (defense <= 0.0f) { settlement->siegeProgress = 1.0f; }
            else
            {
                // A siege is a slow grind: superiority sets the pace, it does not skip the work.
                const f32 ratio = attack / (attack + defense);
                settlement->siegeProgress += ratio * 0.05f * days;
            }

            // Starving the walls also costs the besiegers.
            for (EntityId unitId : cohort.units)
            {
                if (Unit* unit = world.FindUnit(unitId))
                {
                    unit->fatigue = Clamp01(unit->fatigue + 0.005f * days);
                }
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

        // The victors take what they can carry.
        const f32 loot = config.Float("battle/lootFraction", 0.35f);
        conqueror->resources.money += static_cast<f32>(settlement->population) * 0.15f * loot;
        conqueror->resources.food += static_cast<f32>(settlement->population) * 0.05f * loot;

        settlement->owner = newOwner;
        settlement->siegeProgress = 0.0f;
        settlement->besiegedBy = kInvalidId;
        settlement->loyalty = std::max(0.12f, settlement->loyalty * 0.4f);
        settlement->prosperity *= 1.0f - config.Float("battle/razeProsperityLoss", 0.6f) * 0.5f;
        settlement->population = std::max(30, static_cast<i32>(settlement->population * 0.88f));
        conqueror->AddSettlement(settlementId);

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

            Clan* raider = world.FindClan(cohort.clan);
            if (!raider) { cohort.currentTask.Clear(); continue; }

            const f32 population = static_cast<f32>(settlement->population);
            raider->resources.money += population * action["lootMoneyPerPopulation"].AsFloat(0.12f);
            raider->resources.food += population * action["lootFoodPerPopulation"].AsFloat(0.05f);

            settlement->prosperity = std::max(0.0f, settlement->prosperity -
                                              action["prosperityLoss"].AsFloat(0.35f));
            settlement->loyalty = std::max(0.0f, settlement->loyalty - action["loyaltyLoss"].AsFloat(0.2f));
            settlement->population = std::max(20, static_cast<i32>(
                population * (1.0f - action["populationLoss"].AsFloat(0.1f))));

            world.Log(settlement->name + " пограбовано родом " + raider->name, Color::FromRGB(0xC05046));
            cohort.currentTask.Clear();
        }
    }
}
