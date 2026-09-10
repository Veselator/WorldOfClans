#include "AIPlayer.h"
#include "../Factories/SettlementFactory.h"
#include "../Factories/UnitFactory.h"
#include "../Systems/BattleSystem.h"
#include "../Systems/MovementSystem.h"
#include "../Systems/SettlementSystem.h"
#include "../World/World.h"
#include "../../Core/Config.h"

#include <algorithm>

namespace woc
{
    void AIPlayer::OnThink(World& world, i32 day)
    {
        State* state = world.FindState(m_stateId);
        if (!state || state->eliminated) return;

        // Every clan of the realm acts for itself; the leading house acts first.
        std::vector<EntityId> order = state->clans;
        std::sort(order.begin(), order.end(),
            [state](EntityId a, EntityId b) { return a == state->leader && b != state->leader; });

        for (EntityId clanId : order)
        {
            Clan* clan = world.FindClan(clanId);
            if (!clan || clan->eliminated) continue;

            ManageEconomy(world, *clan);
            ManageMilitary(world, *clan);

            // Expansion is deliberately slow: founding towns everywhere at once looks silly.
            if (day - m_lastExpansionDay > 180)
            {
                ManageExpansion(world, *clan);
                m_lastExpansionDay = day;
            }
        }
    }

    void AIPlayer::ManageEconomy(World& world, Clan& clan)
    {
        ConfigManager& config = ConfigManager::Get();
        SettlementSystem& settlements = SettlementSystem::Get();
        const f32 reserve = config.Float("ai/recruitTreasuryReserve", 120.0f);
        const f32 economyWeight = config.Float("ai/economyWeight", 0.9f);

        for (EntityId settlementId : clan.settlements)
        {
            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;
            if (!settlement->construction.empty()) continue;
            if (clan.resources.money < reserve) break;

            // Score the buildable options and take the best one that is actually affordable.
            const std::vector<BuildOption> options = settlements.BuildOptions(world, settlementId);
            const BuildingInfo* best = nullptr;
            f32 bestScore = 0.0f;

            for (const BuildOption& option : options)
            {
                if (!option.allowed || !option.affordable || !option.building) continue;

                f32 score = 0.0f;
                switch (option.building->decorator)
                {
                case DecoratorKind::Production:
                    score = (option.building->multiplier - 1.0f) * 4.0f + option.building->flat * 0.5f;
                    score *= economyWeight;
                    break;
                case DecoratorKind::Defense:
                    score = (option.building->defenseMultiplier - 1.0f) * 3.0f *
                            config.Float("ai/defenceWeight", 1.2f);
                    break;
                case DecoratorKind::Loyalty:
                    // A restless town is worth calming before it is worth enriching.
                    score = option.building->loyaltyPerMonth * 60.0f * (1.2f - settlement->loyalty);
                    break;
                case DecoratorKind::Coverage:
                    score = (option.building->coverageMultiplier - 1.0f) * 6.0f *
                            config.Float("ai/expansionWeight", 1.0f);
                    break;
                case DecoratorKind::Military:
                    score = option.building->trainingBonus * 8.0f;
                    break;
                }

                // Cheaper is better when two options are otherwise comparable.
                score /= 1.0f + option.building->cost.money / 400.0f;
                if (score > bestScore) { bestScore = score; best = option.building; }
            }

            if (best) settlements.StartConstruction(world, settlementId, best->id);
        }
    }

    EntityId AIPlayer::PickThreatenedSettlement(World& world, const Clan& clan) const
    {
        EntityId worst = kInvalidId;
        f32 worstScore = 0.0f;

        for (EntityId settlementId : clan.settlements)
        {
            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;

            f32 score = 0.0f;
            if (settlement->besiegedBy != kInvalidId) score += 10.0f;
            score += (1.0f - settlement->loyalty) * 2.0f;

            // Enemy armies nearby matter more than distant ones.
            for (const auto& [cohortId, cohort] : world.Cohorts())
            {
                if (!world.AreHostile(clan.id, cohort.clan)) continue;
                const f32 distance = Distance(cohort.position, settlement->position);
                if (distance < 320.0f) score += (320.0f - distance) / 320.0f * 4.0f;
            }

            bool garrisoned = false;
            for (const auto& [cohortId, cohort] : world.Cohorts())
            {
                if (cohort.garrisonOf == settlementId) { garrisoned = true; break; }
            }
            if (!garrisoned) score *= 1.6f;

            if (score > worstScore) { worstScore = score; worst = settlementId; }
        }
        return worstScore > 2.0f ? worst : kInvalidId;
    }

    EntityId AIPlayer::PickOffensiveTarget(World& world, const Clan& clan, const Vec2& from) const
    {
        const f32 limit = ConfigManager::Get().Float("ai/raidDistanceLimit", 900.0f);

        EntityId best = kInvalidId;
        f32 bestScore = 0.0f;

        for (const auto& [settlementId, settlement] : world.Settlements())
        {
            const bool independent = settlement.owner == kInvalidId;
            if (!independent && !world.AreHostile(clan.id, settlement.owner)) continue;
            if (settlement.owner == clan.id) continue;

            const f32 distance = Distance(settlement.position, from);
            if (distance > limit) continue;

            // Value the prize, discount by how far away and how well defended it is.
            f32 score = static_cast<f32>(settlement.population) / 400.0f;
            if (settlement.kind == SettlementKind::City) score *= 1.8f;
            if (settlement.kind == SettlementKind::Castle) score *= 1.3f;
            if (independent) score *= 0.8f;
            score *= 1.0f - distance / (limit * 1.4f);
            score /= 1.0f + BattleSystem::Get().SettlementDefense(world, settlementId) * 0.05f;

            if (score > bestScore) { bestScore = score; best = settlementId; }
        }
        return best;
    }

    void AIPlayer::ManageMilitary(World& world, Clan& clan)
    {
        ConfigManager& config = ConfigManager::Get();
        SettlementSystem& settlements = SettlementSystem::Get();
        MovementSystem& movement = MovementSystem::Get();

        const i32 minGarrison = config.Int("ai/minGarrisonUnits", 2);
        const f32 reserve = config.Float("ai/recruitTreasuryReserve", 120.0f);

        // --- raise troops ---------------------------------------------------------------------
        if (clan.resources.money > reserve * 2.0f)
        {
            for (EntityId settlementId : clan.settlements)
            {
                const Settlement* settlement = world.FindSettlement(settlementId);
                if (!settlement) continue;
                if (settlement->kind == SettlementKind::Village) continue;

                EntityId garrison = kInvalidId;
                i32 units = 0;
                for (const auto& [cohortId, cohort] : world.Cohorts())
                {
                    if (cohort.garrisonOf != settlementId) continue;
                    garrison = cohortId;
                    units = static_cast<i32>(cohort.units.size());
                    break;
                }
                if (units >= minGarrison + 3) continue;

                // A balanced levy: line infantry first, then missiles, then horse.
                static const UnitRole kPreference[] = {
                    UnitRole::Swordsman, UnitRole::Archer, UnitRole::Swordsman, UnitRole::Cavalry
                };
                const UnitRole role = kPreference[static_cast<size_t>(m_random.Range(0, 3))];
                if (garrison == kInvalidId)
                {
                    settlements.Recruit(world, settlementId, kInvalidId, UnitRole::Aristocrat);
                }
                else
                {
                    settlements.Recruit(world, settlementId, garrison, role);
                }
                if (clan.resources.money < reserve * 2.0f) break;
            }
        }

        // --- give the armies something to do ------------------------------------------------------
        const EntityId threatened = PickThreatenedSettlement(world, clan);
        bool defenderAssigned = false;

        for (EntityId cohortId : clan.cohorts)
        {
            Cohort* cohort = world.FindCohort(cohortId);
            if (!cohort || cohort->IsEmpty() || cohort->inBattle) continue;
            if (cohort->currentTask.IsMoving()) continue;
            if (cohort->currentTask.type == TaskType::Besiege ||
                cohort->currentTask.type == TaskType::Raid) continue;

            // A weakened or unsupplied force goes home before it does anything else.
            if (cohort->supply < 0.3f || world.CohortStrength(cohortId) < 40)
            {
                const Settlement* home = world.NearestSettlement(cohort->position, 1e9f, clan.id);
                if (home && cohort->garrisonOf != home->id)
                {
                    movement.OrderTask(world, cohortId, TaskType::Garrison, home->position, home->id);
                }
                continue;
            }

            if (!defenderAssigned && threatened != kInvalidId)
            {
                const Settlement* settlement = world.FindSettlement(threatened);
                if (settlement && cohort->garrisonOf != threatened)
                {
                    movement.OrderTask(world, cohortId, TaskType::Garrison, settlement->position, threatened);
                    defenderAssigned = true;
                    continue;
                }
            }

            const EntityId target = PickOffensiveTarget(world, clan, cohort->position);
            if (target == kInvalidId) continue;

            const Settlement* settlement = world.FindSettlement(target);
            if (!settlement) continue;

            // Attack only with a real prospect of success; otherwise raid and withdraw.
            const f32 power = world.CohortPower(cohortId) * 0.01f;
            const f32 defense = BattleSystem::Get().SettlementDefense(world, target);
            const TaskType task = (power > defense * 1.15f) ? TaskType::Besiege : TaskType::Raid;

            if (task == TaskType::Raid && settlement->owner == kInvalidId) continue;
            movement.OrderTask(world, cohortId, task, settlement->position, target);
        }
    }

    void AIPlayer::ManageExpansion(World& world, Clan& clan)
    {
        ConfigManager& config = ConfigManager::Get();
        SettlementSystem& settlements = SettlementSystem::Get();

        if (clan.settlements.empty()) return;
        if (config.Float("ai/expansionWeight", 1.0f) <= 0.0f) return;

        // Build outwards from an existing holding: a castle to hold new ground, or a
        // village where the land is good and the treasury is thin.
        const EntityId anchorId = clan.settlements[
            static_cast<size_t>(m_random.Range(0, static_cast<i32>(clan.settlements.size()) - 1))];
        const Settlement* anchor = world.FindSettlement(anchorId);
        if (!anchor) return;

        const SettlementKind kind = clan.resources.CanAfford(
            SettlementDatabase::Get().Kind(SettlementKind::Castle).buildCost)
            ? SettlementKind::Castle
            : SettlementKind::Village;

        Vec2 site;
        if (!SettlementFactory::FindSite(world, world.Map(), kind, anchor->position, 300.0f, m_random, site))
        {
            return;
        }
        settlements.Found(world, clan.id, kind, site);
    }
}
