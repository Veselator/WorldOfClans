#include "SettlementDecorators.h"
#include "../Map/MapData.h"
#include "../World/RaceDatabase.h"
#include "../../Core/Config.h"

namespace woc
{
    SettlementBaseEvaluator::SettlementBaseEvaluator(const Settlement& settlement, const MapData& map)
        : m_settlement(settlement), m_map(map)
    {
        ConfigManager& config = ConfigManager::Get();
        m_workRadius = config.Float("forestry/fieldMaxRadius", 46.0f) * 2.0f;

        m_soil = m_map.SampleSoil(settlement.position, m_workRadius);
        m_forest = m_map.SampleForest(settlement.position, m_workRadius);
        m_stone = m_map.SampleStone(settlement.position, m_workRadius);
        m_field = m_map.SampleField(settlement.position, m_workRadius);
        m_defenseTerrain = m_map.TerrainAtMap(settlement.position).defense;
        m_tilesInRadius = m_map.CountTilesInRadius(settlement.position, m_workRadius);
    }

    f32 SettlementBaseEvaluator::Production(ResourceType resource) const
    {
        ConfigManager& config = ConfigManager::Get();
        const SettlementKindInfo& kind = m_settlement.KindInfo();
        const SettlementTier& tier = m_settlement.Tier();
        const RaceInfo& race = RaceDatabase::Get().Race(m_settlement.raceId);

        const f32 population = static_cast<f32>(m_settlement.population);
        f32 output = 0.0f;

        switch (resource)
        {
        case ResourceType::Food:
        {
            if (kind.kind == SettlementKind::Castle) break;
            // Villages feed the realm; a city grows some of its own bread but less of it.
            const f32 villageWeight = kind.kind == SettlementKind::Village ? 1.0f : 0.45f;
            output = population * config.Float("economy/foodPerVillagePopulation", 0.02f) *
                     tier.production * m_soil * villageWeight;
            output += m_field * static_cast<f32>(m_tilesInRadius) *
                      config.Float("economy/foodPerFieldTile", 0.045f);
            output *= race.modifiers.foodProduction;
            break;
        }
        case ResourceType::Money:
        {
            if (kind.kind == SettlementKind::Castle) break;
            const f32 cityWeight = kind.kind == SettlementKind::City ? 1.0f : 0.28f;
            output = population * config.Float("economy/moneyPerCityPopulation", 0.0125f) *
                     tier.production * cityWeight;
            output *= race.modifiers.moneyProduction;
            break;
        }
        case ResourceType::Wood:
        {
            // Peasants always cut a little; a sawmill is what turns it into revenue.
            output = m_forest * static_cast<f32>(m_tilesInRadius) *
                     config.Float("economy/woodPerForestTile", 0.0035f);
            output *= race.modifiers.woodProduction;
            break;
        }
        case ResourceType::Stone:
        {
            output = m_stone * config.Float("economy/stonePerQuarry", 1.6f) * 0.18f;
            output *= race.modifiers.stoneProduction;
            break;
        }
        default: break;
        }

        // Prosperity and loyalty scale everything a settlement does.
        output *= 0.55f + Clamp01(m_settlement.prosperity) * 0.85f;
        output *= 0.6f + Clamp01(m_settlement.loyalty) * 0.4f;
        return std::max(0.0f, output);
    }

    f32 SettlementBaseEvaluator::Defense() const
    {
        const SettlementTier& tier = m_settlement.Tier();
        f32 defense = SettlementDatabase::Get().DefenseBase() * m_defenseTerrain;
        // Bigger places are simply harder to take, walls or not.
        defense *= 1.0f + static_cast<f32>(tier.tier - 1) * 0.2f;
        if (m_settlement.kind == SettlementKind::Castle) defense *= 1.6f;
        defense *= 0.7f + Clamp01(m_settlement.loyalty) * 0.5f;
        return defense;
    }

    f32 SettlementBaseEvaluator::Coverage() const
    {
        const RaceInfo& race = RaceDatabase::Get().Race(m_settlement.raceId);
        f32 coverage = m_settlement.BaseCoverage() * race.modifiers.coverage;
        // A place in turmoil cannot project authority over the countryside.
        coverage *= 0.65f + Clamp01(m_settlement.loyalty) * 0.35f;
        return coverage;
    }

    f32 SettlementBaseEvaluator::LoyaltyDrift() const
    {
        ConfigManager& config = ConfigManager::Get();
        const RaceInfo& race = RaceDatabase::Get().Race(m_settlement.raceId);

        f32 drift = config.Float("population/loyaltyDrift", 0.012f) * race.modifiers.loyalty;
        drift *= 0.4f + Clamp01(m_settlement.prosperity);
        return drift;
    }

    f32 SettlementBaseEvaluator::RecruitCostMultiplier(UnitRole role) const
    {
        (void)role;
        return RaceDatabase::Get().Race(m_settlement.raceId).modifiers.recruitCost;
    }

    f32 SettlementBaseEvaluator::MoraleBonus() const
    {
        return RaceDatabase::Get().Faith(m_settlement.faithId).moraleBonus;
    }

    f32 SettlementBaseEvaluator::ForestHarvest() const
    {
        // Villages nibble at the woods on their own, without any sawmill.
        if (m_settlement.kind == SettlementKind::Castle) return 0.0f;
        return ConfigManager::Get().Float("forestry/villageHarvestPerMonth", 0.9f);
    }

    f32 SettlementBaseEvaluator::SiegeSupplyDays() const
    {
        return SettlementDatabase::Get().SiegeSupplyDays();
    }
}
