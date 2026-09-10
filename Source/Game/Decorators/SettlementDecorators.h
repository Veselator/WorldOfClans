// SettlementDecorators.h - the base evaluator and the five building decorators.
#pragma once

#include "ISettlementEvaluator.h"

namespace woc
{
    class MapData;

    /// Evaluates a settlement stripped of every building: terrain, people and tier only.
    class SettlementBaseEvaluator final : public ISettlementEvaluator
    {
    public:
        SettlementBaseEvaluator(const Settlement& settlement, const MapData& map);

        f32 Production(ResourceType resource) const override;
        f32 Defense() const override;
        f32 Coverage() const override;
        f32 LoyaltyDrift() const override;
        f32 TrainingBonus() const override { return 0.0f; }
        f32 RecruitCostMultiplier(UnitRole role) const override;
        f32 CaravanBonus() const override { return 0.0f; }
        f32 ConversionCostMultiplier() const override { return 1.0f; }
        f32 DistancePenaltyMultiplier() const override { return 1.0f; }
        f32 MoraleBonus() const override;
        f32 ForestHarvest() const override;
        f32 FieldRadiusBonus() const override { return 0.0f; }
        f32 SiegeSupplyDays() const override;

    private:
        const Settlement& m_settlement;
        const MapData& m_map;

        f32 m_soil = 1.0f;
        f32 m_forest = 0.0f;
        f32 m_stone = 0.0f;
        f32 m_field = 0.0f;
        f32 m_defenseTerrain = 1.0f;
        u32 m_tilesInRadius = 0;
        f32 m_workRadius = 90.0f;
    };

    class ProductionDecorator final : public SettlementDecorator
    {
    public:
        using SettlementDecorator::SettlementDecorator;
        f32 Production(ResourceType resource) const override
        {
            const f32 inner = m_inner->Production(resource);
            if (resource != m_building.resource) return inner;
            return inner * m_building.multiplier + m_building.flat;
        }
    };

    class DefenseDecorator final : public SettlementDecorator
    {
    public:
        using SettlementDecorator::SettlementDecorator;
        f32 Defense() const override { return m_inner->Defense() * m_building.defenseMultiplier; }
    };

    class MilitaryDecorator final : public SettlementDecorator
    {
    public:
        using SettlementDecorator::SettlementDecorator;
        f32 TrainingBonus() const override { return m_inner->TrainingBonus() + m_building.trainingBonus; }
        f32 RecruitCostMultiplier(UnitRole role) const override
        {
            f32 multiplier = m_inner->RecruitCostMultiplier(role) * m_building.recruitCostMultiplier;
            if (role == UnitRole::Cavalry || role == UnitRole::HorseArcher)
                multiplier *= m_building.cavalryCostMultiplier;
            return multiplier;
        }
    };

    class LoyaltyDecorator final : public SettlementDecorator
    {
    public:
        using SettlementDecorator::SettlementDecorator;
        f32 LoyaltyDrift() const override { return m_inner->LoyaltyDrift() + m_building.loyaltyPerMonth; }
        f32 ConversionCostMultiplier() const override
        {
            return m_inner->ConversionCostMultiplier() * m_building.conversionCostMultiplier;
        }
        f32 DistancePenaltyMultiplier() const override
        {
            return m_inner->DistancePenaltyMultiplier() * m_building.distancePenaltyMultiplier;
        }
    };

    class CoverageDecorator final : public SettlementDecorator
    {
    public:
        using SettlementDecorator::SettlementDecorator;
        f32 Coverage() const override { return m_inner->Coverage() * m_building.coverageMultiplier; }
    };
}
