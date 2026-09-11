#include "EconomySystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../World/World.h"
#include "../../Core/Config.h"

#include <algorithm>

namespace woc
{
    ResourceData EconomySystem::SettlementOutput(World& world, EntityId settlementId) const
    {
        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return {};

        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(*settlement, world.Map());

        ResourceData output;
        output.food = evaluator->Production(ResourceType::Food);
        output.wood = evaluator->Production(ResourceType::Wood);
        output.stone = evaluator->Production(ResourceType::Stone);
        output.money = evaluator->Production(ResourceType::Money);

        // Trade caravans need roads to travel on; a market without a road earns less.
        const f32 caravan = evaluator->CaravanBonus();
        if (caravan > 0.0f)
        {
            const Coord tile = world.Map().ToTile(settlement->position);
            const bool onRoad = world.Map().At(tile).road > 0;
            output.money *= 1.0f + caravan * (onRoad ? 1.0f : 0.4f);
        }

        // A settlement under siege produces nothing worth collecting.
        if (settlement->besiegedBy != kInvalidId) output = output * 0.15f;
        return output;
    }

    ResourceData EconomySystem::ClanUpkeep(World& world, EntityId clanId) const
    {
        ConfigManager& config = ConfigManager::Get();
        const Clan* clan = world.FindClan(clanId);
        if (!clan) return {};

        ResourceData upkeep;
        for (EntityId settlementId : clan->settlements)
        {
            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;
            switch (settlement->kind)
            {
            case SettlementKind::Castle:  upkeep.money += config.Float("economy/castleUpkeep", 4.0f); break;
            case SettlementKind::City:    upkeep.money += config.Float("economy/cityUpkeep", 3.0f); break;
            default:                      upkeep.money += config.Float("economy/villageUpkeep", 0.5f); break;
            }
        }

        const f32 foodPerHead = config.Float("economy/foodPerUnitPerMonth", 0.02f);
        for (EntityId cohortId : clan->cohorts)
        {
            const Cohort* cohort = world.FindCohort(cohortId);
            if (!cohort) continue;
            for (EntityId unitId : cohort->units)
            {
                const Unit* unit = world.FindUnit(unitId);
                if (!unit) continue;
                upkeep.money += unit->Stats().upkeep * static_cast<f32>(unit->Strength()) *
                                config.Float("economy/cohortUpkeepPerUnit", 0.04f) * 25.0f;
                upkeep.food += foodPerHead * static_cast<f32>(unit->Strength());
            }
        }
        return upkeep;
    }

    ClanBudget EconomySystem::Preview(World& world, EntityId clanId) const
    {
        ClanBudget budget;
        const Clan* clan = world.FindClan(clanId);
        if (!clan) return budget;

        for (EntityId settlementId : clan->settlements)
        {
            budget.income += SettlementOutput(world, settlementId);
        }

        // A quarry the clan has opened yields stone on its own, whatever the ground around
        // the nearest town happens to be made of.
        const f32 perMine = ConfigManager::Get().Float("economy/stonePerDevelopedMine", 5.5f);
        for (const MineSite& mine : world.Mines())
        {
            if (!mine.developed || mine.owner != clanId) continue;
            budget.income.stone += mine.richness * perMine;
        }
        budget.upkeep = ClanUpkeep(world, clanId);
        budget.foodConsumption = budget.upkeep.food;
        budget.net = budget.income - budget.upkeep;
        budget.starving = (clan->resources.food + budget.net.food) < 0.0f;
        return budget;
    }

    void EconomySystem::Tick(World& world, i32 days)
    {
        if (days <= 0) return;

        // Everything in the budget is quoted per month, so a settlement run on a fortnight
        // pays out half of it. The year's total is the same whatever the cadence.
        const f32 monthDays = static_cast<f32>(std::max(1, ConfigManager::Get().Int("simulation/daysPerMonth", 30)));
        const f32 share = static_cast<f32>(days) / monthDays;

        for (auto& [clanId, clan] : world.Clans())
        {
            if (clan.eliminated) continue;

            const ClanBudget budget = Preview(world, clanId);
            clan.resources += budget.net * share;
            ApplyShortages(world, clan, budget);
            clan.resources.ClampNonNegative();
        }
    }

    void EconomySystem::ApplyShortages(World& world, Clan& clan, const ClanBudget& budget)
    {
        ConfigManager& config = ConfigManager::Get();

        // Empty granaries and an empty treasury both cost the lord his people's patience.
        f32 loyaltyPenalty = 0.0f;
        if (clan.resources.food < 0.0f)
        {
            loyaltyPenalty += config.Float("economy/starvationLoyaltyPenalty", 0.05f);

            // Hungry armies melt away long before hungry towns do.
            for (EntityId cohortId : clan.cohorts)
            {
                Cohort* cohort = world.FindCohort(cohortId);
                if (!cohort) continue;
                for (EntityId unitId : cohort->units)
                {
                    if (Unit* unit = world.FindUnit(unitId))
                    {
                        unit->morale = std::max(0.05f, unit->morale - 0.12f);
                    }
                }
            }
            world.Log("У землях роду " + clan.name + " голод", Color::FromRGB(0xC05046));
        }
        if (clan.resources.money < 0.0f)
        {
            loyaltyPenalty += config.Float("economy/treasuryDebtLoyaltyPenalty", 0.03f);

            // Unpaid soldiers walk home. How many depends on how deep the debt is relative
            // to what the month's wages were supposed to be.
            const f32 wages = std::max(1.0f, budget.upkeep.money);
            const f32 shortfall = Clamp01(-clan.resources.money / wages);
            const f32 rate = config.Float("economy/desertionRate", 0.35f) * (0.25f + shortfall);

            u32 deserters = 0;
            std::vector<EntityId> emptied;
            for (EntityId cohortId : clan.cohorts)
            {
                Cohort* cohort = world.FindCohort(cohortId);
                if (!cohort) continue;

                for (EntityId unitId : cohort->units)
                {
                    Unit* unit = world.FindUnit(unitId);
                    if (!unit || unit->IsDestroyed()) continue;

                    u32 leaving = static_cast<u32>(unit->Strength() * rate);
                    if (leaving == 0 && unit->Strength() > 0 && rate > 0.05f) leaving = 1;
                    leaving = std::min(leaving, unit->Strength());

                    for (u32 i = 0; i < leaving; ++i)
                    {
                        const EntityId characterId = unit->characters.back();
                        unit->characters.pop_back();
                        world.Characters().erase(characterId);
                    }
                    deserters += leaving;
                    unit->morale = std::max(0.05f, unit->morale - 0.10f);
                    if (unit->IsDestroyed()) emptied.push_back(unitId);
                }
            }

            for (EntityId unitId : emptied) world.DestroyUnit(unitId);

            if (deserters > 0)
            {
                world.Log("Скарбниця роду " + clan.name + " порожня: " +
                          std::to_string(deserters) + " воїнів розійшлися по домівках",
                          Color::FromRGB(0xD2933A));
            }
        }

        if (loyaltyPenalty > 0.0f)
        {
            for (EntityId settlementId : clan.settlements)
            {
                if (Settlement* settlement = world.FindSettlement(settlementId))
                {
                    settlement->loyalty = std::max(0.0f, settlement->loyalty - loyaltyPenalty);
                }
            }
        }
    }
}
