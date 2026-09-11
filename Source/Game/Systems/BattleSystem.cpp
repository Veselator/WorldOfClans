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
            if (battle.attacker.cohort == cohortId || battle.defender.cohort == cohortId) return &battle;
        }
        return nullptr;
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
            const EntityId enemyId = battle->attacker.cohort == cohortId
                                   ? battle->defender.cohort : battle->attacker.cohort;
            if (const Cohort* enemy = world.FindCohort(enemyId))
            {
                u32 dead = 0, hurt = 0;
                ApplyDamage(world, *cohort,
                            EvaluatePower(world, enemy->id, cohort->id, enemy->position) *
                            config.Float("battle/baseDamage", 0.06f) *
                            config.Float("battle/withdrawPartingBlow", 0.8f),
                            dead, hurt, config.Float("battle/withdrawKillShare", 0.5f));

                const Vec2 away = (cohort->position - enemy->position).Normalized();
                cohort->position += away * config.Float("battle/withdrawDistance", 55.0f);
            }
        }

        cohort->organisation = Clamp01(cohort->organisation -
                                       config.Float("battle/withdrawOrganisation", 0.25f));
        cohort->disengageDays = config.Float("battle/disengageDays", 3.0f);
        cohort->inBattle = false;
        cohort->currentTask.Clear();

        const Clan* clan = world.FindClan(cohort->clan);
        world.Log(cohort->DisplayName() + " виходить з бою",
                  clan ? clan->color : Color::FromRGB(0x9AA3AB));
        return true;
    }

    void BattleSystem::TickRecovery(World& world, f32 days)
    {
        // Recovery and attrition are counted by the day, not by the frame.
        m_recoveryCarry += days;
        if (m_recoveryCarry < 1.0f) return;

        const f32 elapsed = std::floor(m_recoveryCarry);
        m_recoveryCarry -= elapsed;

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

            a->inBattle = b->inBattle = true;
            battle.elapsedDays += days;
            battle.position = (a->position + b->position) * 0.5f;

            battle.roundProgress += roundsPerDay * days;
            const i32 rounds = static_cast<i32>(battle.roundProgress);
            battle.roundProgress -= static_cast<f32>(rounds);

            if (rounds > 0) RunRounds(world, battle, rounds);
            if (battle.concluded)
            {
                a->inBattle = false;
                b->inBattle = false;
                continue;
            }
            carried.push_back(battle);
        }

        // --- fresh contacts ----------------------------------------------------------------
        // Each cohort fights at most one battle, which keeps a melee of many armies from
        // resolving in an arbitrary order.
        std::vector<EntityId> ids;
        ids.reserve(world.Cohorts().size());
        for (const auto& [id, cohort] : world.Cohorts()) ids.push_back(id);
        std::sort(ids.begin(), ids.end());

        auto alreadyFighting = [&carried](EntityId id)
        {
            for (const BattleReport& battle : carried)
            {
                if (battle.attacker.cohort == id || battle.defender.cohort == id) return true;
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

                report.roundProgress = roundsPerDay * days;
                const i32 rounds = static_cast<i32>(report.roundProgress);
                report.roundProgress -= static_cast<f32>(rounds);
                report.elapsedDays = days;
                if (rounds > 0) RunRounds(world, report, rounds);

                if (!report.concluded) carried.push_back(report);
                else { a->inBattle = false; b->inBattle = false; }
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
            f32 damageToB = powerA * baseDamage * random.RangeF(0.85f, 1.15f);
            f32 damageToA = powerB * baseDamage * random.RangeF(0.85f, 1.15f);

            // Nobody walks off a field untouched. However lopsided the odds, men with
            // spears in their hands take some toll simply by being there, so each side
            // deals at least a scratch proportional to how many of them are swinging.
            const f32 bite = config.Float("battle/minBitePerHead", 0.9f);
            damageToA = std::max(damageToA, static_cast<f32>(world.CohortStrength(b->id)) * bite);
            damageToB = std::max(damageToB, static_cast<f32>(world.CohortStrength(a->id)) * bite);

            // Most of what a line of battle takes is men down, not men dead: a shield wall
            // wounds far more than it kills. The killing happens in the pursuit.
            const f32 killShare = config.Float("battle/meleeKillShare", 0.4f);
            u32 deadA = 0, deadB = 0, hurtA = 0, hurtB = 0;
            ApplyDamage(world, *b, damageToB, deadB, hurtB, killShare);
            ApplyDamage(world, *a, damageToA, deadA, hurtA, killShare);
            report.attacker.losses += deadA;
            report.defender.losses += deadB;
            report.attacker.hurt += hurtA;
            report.defender.hurt += hurtB;
            const u32 lossesA = deadA + hurtA;
            const u32 lossesB = deadB + hurtB;

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

            // Every round of melee costs order on both sides.
            const f32 shock = ConfigManager::Get().Float("battle/organisationPerRound", 0.06f);
            a->organisation = Clamp01(a->organisation - shock);
            b->organisation = Clamp01(b->organisation - shock);

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

                // The pursuit is where the dying is done - but a broken host is not an
                // annihilated one. It runs, loses its order, and can be rallied.
                u32 pursuitDead = 0, pursuitHurt = 0;
                ApplyDamage(world, *loser,
                            EvaluatePower(world, winner->id, loser->id, winner->position) *
                            baseDamage * config.Float("battle/pursuitDamage", 1.5f),
                            pursuitDead, pursuitHurt,
                            config.Float("battle/pursuitKillShare", 0.7f));

                BattleSide& losingSide = attackerBroke ? report.attacker : report.defender;
                losingSide.losses += pursuitDead;
                losingSide.hurt += pursuitHurt;

                // A rearguard that turns and fights, and the horses that founder in the
                // chase: running a broken army down is not free either.
                u32 chaseDead = 0, chaseHurt = 0;
                ApplyDamage(world, *winner,
                            static_cast<f32>(world.CohortStrength(loser->id)) *
                            config.Float("battle/pursuitCostPerHead", 0.5f),
                            chaseDead, chaseHurt, config.Float("battle/meleeKillShare", 0.4f));

                BattleSide& winningSide = attackerBroke ? report.defender : report.attacker;
                winningSide.losses += chaseDead;
                winningSide.hurt += chaseHurt;

                const Vec2 away = (loser->position - winner->position).Normalized();
                loser->position += away * config.Float("battle/routDistance", 60.0f);
                loser->currentTask.Clear();
                loser->organisation = Clamp01(loser->organisation *
                                              config.Float("battle/routOrganisation", 0.35f));
                loser->disengageDays = config.Float("battle/disengageDays", 3.0f);
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
            // No battle is won for nothing. If the chances happened to spare the winner
            // entirely - a brief skirmish against a host that broke at once - he still
            // carries somebody back to camp. A victory in the chronicle with no cost
            // beside it reads as a rounding error, and it is one.
            auto exactToll = [&](BattleSide& side)
            {
                if (side.losses + side.hurt > 0) return;
                Cohort* host = world.FindCohort(side.cohort);
                if (!host || host->IsEmpty()) return;

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
            };
            if (report.victor == report.attacker.clan) exactToll(report.attacker);
            else if (report.victor == report.defender.clan) exactToll(report.defender);

            const Clan* victor = world.FindClan(report.victor);
            const std::string where = report.terrainName;
            auto toll = [](const BattleSide& side)
            {
                return std::to_string(side.losses) + " полеглих, " +
                       std::to_string(side.hurt) + " поранених";
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

            world.Log(settlement->name + " пограбовано родом " + raider->name, Color::FromRGB(0xC05046));
            cohort.currentTask.Clear();
        }
    }
}
