#include "PopulationSystem.h"
#include "CoverageSystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../Factories/SettlementFactory.h"
#include "../World/RaceDatabase.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Random.h"

#include <algorithm>

namespace woc
{
    f32& PopulationSystem::MigrantsFor(EntityId clanId)
    {
        for (auto& entry : m_migrantPool)
        {
            if (entry.first == clanId) return entry.second;
        }
        m_migrantPool.emplace_back(clanId, 0.0f);
        return m_migrantPool.back().second;
    }

    void PopulationSystem::Tick(World& world)
    {
        for (auto& [id, settlement] : world.Settlements())
        {
            UpdateProsperity(world, settlement);
            UpdateLoyalty(world, settlement);
            GrowPopulation(world, settlement);
        }
        HandleRevolts(world);
        SpawnVillages(world);
    }

    void PopulationSystem::GrowPopulation(World& world, Settlement& settlement)
    {
        ConfigManager& config = ConfigManager::Get();
        const RaceInfo& race = RaceDatabase::Get().Race(settlement.raceId);
        const SettlementTier& tier = settlement.Tier();

        f32 rate = config.Float("population/baseGrowthRate", 0.012f) * race.modifiers.populationGrowth;
        rate += settlement.prosperity * config.Float("population/prosperityGrowthFactor", 0.02f);
        rate += (settlement.loyalty - 0.5f) * config.Float("population/loyaltyGrowthFactor", 0.008f);

        // Past the tier's ceiling growth stalls and the surplus becomes migrants.
        const f32 ceiling = static_cast<f32>(tier.maxPopulation);
        const f32 crowding = static_cast<f32>(settlement.population) / std::max(1.0f, ceiling);
        if (crowding > 0.9f)
        {
            rate *= 1.0f - std::min(1.0f, (crowding - 0.9f) * 10.0f) *
                           config.Float("population/overcrowdingPenalty", 0.5f);
        }

        if (settlement.besiegedBy != kInvalidId) rate = std::min(rate, -0.02f);

        const f32 growth = static_cast<f32>(settlement.population) * rate;
        settlement.population = std::max(20, settlement.population + static_cast<i32>(growth));

        // Overflow leaves in search of new land.
        if (settlement.population > tier.maxPopulation)
        {
            const i32 surplus = settlement.population - tier.maxPopulation;
            settlement.population -= surplus / 2;
            if (settlement.owner != kInvalidId)
            {
                MigrantsFor(settlement.owner) += static_cast<f32>(surplus / 2);
            }
        }
        else if (settlement.kind == SettlementKind::Village && settlement.owner != kInvalidId)
        {
            MigrantsFor(settlement.owner) += static_cast<f32>(settlement.population) *
                                             config.Float("population/migrantsPerMonthFactor", 0.004f);
        }
    }

    void PopulationSystem::UpdateProsperity(World& world, Settlement& settlement)
    {
        ConfigManager& config = ConfigManager::Get();
        const SettlementTier& tier = settlement.Tier();
        const f32 drift = config.Float("population/prosperityDrift", 0.01f);

        f32 target = tier.prosperityCap * (0.4f + settlement.loyalty * 0.6f);
        if (settlement.besiegedBy != kInvalidId) target = 0.05f;

        // Fields and roads around a settlement make it visibly richer.
        const f32 fields = world.Map().SampleField(settlement.position, 80.0f);
        target += fields * 0.2f;
        target = std::clamp(target, 0.0f, tier.prosperityCap + 0.25f);

        settlement.prosperity += (target - settlement.prosperity) * drift * 3.0f;
        settlement.prosperity = Clamp01(settlement.prosperity);
    }

    f32 PopulationSystem::LoyaltyForecast(World& world, EntityId settlementId) const
    {
        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return 0.0f;

        ConfigManager& config = ConfigManager::Get();
        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(*settlement, world.Map());

        f32 delta = evaluator->LoyaltyDrift();
        delta += RaceDatabase::Get().Faith(settlement->faithId).loyaltyBonus * 0.1f;

        const Clan* clan = world.FindClan(settlement->owner);
        if (clan)
        {
            if (settlement->raceId != clan->raceId)
                delta -= config.Float("population/loyaltyForeignRacePenalty", 0.18f) * 0.1f;
            if (settlement->faithId != clan->faithId)
                delta -= config.Float("population/loyaltyForeignFaithPenalty", 0.12f) * 0.1f;

            // Distance from the clan's seat of power erodes obedience.
            const Settlement* capital = nullptr;
            for (EntityId owned : clan->settlements)
            {
                const Settlement* candidate = world.FindSettlement(owned);
                if (!candidate) continue;
                if (!capital || candidate->kind == SettlementKind::City) capital = candidate;
                if (capital && capital->kind == SettlementKind::City) break;
            }
            if (capital && capital->id != settlement->id)
            {
                const f32 distance = Distance(capital->position, settlement->position);
                delta -= distance * config.Float("population/loyaltyDistancePenalty", 0.00035f) *
                         evaluator->DistancePenaltyMultiplier();
            }
        }
        else
        {
            // Independent settlements drift towards contented self-rule.
            delta += 0.02f;
        }

        if (settlement->besiegedBy != kInvalidId) delta -= 0.06f;
        if (settlement->conversionDaysLeft > 0) delta -= 0.02f;
        return delta;
    }

    void PopulationSystem::UpdateLoyalty(World& world, Settlement& settlement)
    {
        settlement.loyalty = Clamp01(settlement.loyalty + LoyaltyForecast(world, settlement.id));
    }

    void PopulationSystem::HandleRevolts(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 threshold = config.Float("population/revoltThreshold", 0.12f);

        std::vector<EntityId> revolting;
        for (const auto& [id, settlement] : world.Settlements())
        {
            if (settlement.owner == kInvalidId) continue;
            if (settlement.loyalty > threshold) continue;
            if (settlement.besiegedBy != kInvalidId) continue;
            revolting.push_back(id);
        }

        for (EntityId id : revolting)
        {
            Settlement* settlement = world.FindSettlement(id);
            if (!settlement) continue;
            Clan* clan = world.FindClan(settlement->owner);

            // Villages simply stop obeying; towns and castles need an army to be lost.
            if (settlement->kind != SettlementKind::Village) continue;

            if (clan) clan->RemoveSettlement(id);
            settlement->owner = kInvalidId;
            settlement->loyalty = 0.6f;
            settlement->rebelliousUntilDay = world.Time().TotalDays() +
                config.Int("population/revoltCooldownDays", 720);
            world.Log(settlement->name + " відклався й більше нікому не платить данини",
                      Color::FromRGB(0xD2933A));
            CoverageSystem::Get().MarkDirty();
        }
    }

    void PopulationSystem::SpawnVillages(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 required = config.Float("population/migrantsForNewVillage", 260.0f);
        const f32 searchRadius = config.Float("population/newVillageMaxDistanceFromParent", 220.0f);

        Random& random = GlobalRandom();

        for (auto& [clanId, migrants] : m_migrantPool)
        {
            if (migrants < required) continue;

            Clan* clan = world.FindClan(clanId);
            if (!clan || clan->settlements.empty()) { migrants = 0.0f; continue; }

            // New villages sprout near an existing one - people move, they do not teleport.
            const EntityId parentId = clan->settlements[
                static_cast<size_t>(random.Range(0, static_cast<i32>(clan->settlements.size()) - 1))];
            const Settlement* parent = world.FindSettlement(parentId);
            if (!parent) { migrants = 0.0f; continue; }

            Vec2 site;
            if (!SettlementFactory::FindSite(world, world.Map(), SettlementKind::Village,
                                             parent->position, searchRadius, random, site))
            {
                // Nowhere to put them this month; keep the pool and try again later.
                continue;
            }

            SettlementRequest request;
            request.kind = SettlementKind::Village;
            request.position = site;
            request.raceId = parent->raceId;
            request.faithId = parent->faithId;
            request.owner = clanId;
            request.population = static_cast<i32>(required * 0.7f);

            Settlement& village = SettlementFactory::Create(world, request, random);
            migrants -= required;

            world.Log("Переселенці заснували село " + village.name, clan->color);
            CoverageSystem::Get().MarkDirty();
        }
    }
}
