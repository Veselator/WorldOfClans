// ISettlementEvaluator.h - the decorator chain that turns a settlement into numbers.
//
// A bare settlement is evaluated by SettlementBaseEvaluator. Every building it owns wraps
// that evaluator in a decorator which adjusts exactly one aspect - production, defence,
// recruitment, loyalty or coverage - and forwards the rest. Adding a building type is
// therefore a JSON entry plus, at most, one new decorator.
#pragma once

#include "../World/Settlement.h"
#include "../World/UnitDatabase.h"

namespace woc
{
    class MapData;

    class ISettlementEvaluator
    {
    public:
        virtual ~ISettlementEvaluator() = default;

        /// Monthly output of one resource.
        virtual f32 Production(ResourceType resource) const = 0;
        /// Multiplier applied to the defender's strength during a siege or assault.
        virtual f32 Defense() const = 0;
        /// Coverage budget: how far the settlement's authority can reach.
        virtual f32 Coverage() const = 0;
        /// Loyalty change per month contributed by the settlement itself.
        virtual f32 LoyaltyDrift() const = 0;
        /// Additive training bonus for units recruited or stationed here.
        virtual f32 TrainingBonus() const = 0;
        /// Multiplier on the cost of recruiting a unit of the given role.
        virtual f32 RecruitCostMultiplier(UnitRole role) const = 0;
        /// Extra money share from trade caravans.
        virtual f32 CaravanBonus() const = 0;
        /// Multiplier on the cost of a religious conversion.
        virtual f32 ConversionCostMultiplier() const = 0;
        /// How much distance from the capital hurts loyalty (courts soften it).
        virtual f32 DistancePenaltyMultiplier() const = 0;
        /// Extra morale for units raised or resting here.
        virtual f32 MoraleBonus() const = 0;
        /// Forest cut down per month around the settlement.
        virtual f32 ForestHarvest() const = 0;
        /// Extra radius the settlement's fields may spread over.
        virtual f32 FieldRadiusBonus() const = 0;
        /// Days of food a garrison can hold out on.
        virtual f32 SiegeSupplyDays() const = 0;
    };

    /// Common forwarding implementation; concrete decorators override only what they change.
    class SettlementDecorator : public ISettlementEvaluator
    {
    public:
        SettlementDecorator(Scope<ISettlementEvaluator> inner, const BuildingInfo& building)
            : m_inner(std::move(inner)), m_building(building) {}

        f32 Production(ResourceType resource) const override { return m_inner->Production(resource); }
        f32 Defense() const override { return m_inner->Defense(); }
        f32 Coverage() const override { return m_inner->Coverage(); }
        f32 LoyaltyDrift() const override { return m_inner->LoyaltyDrift(); }
        f32 TrainingBonus() const override { return m_inner->TrainingBonus(); }
        f32 RecruitCostMultiplier(UnitRole role) const override { return m_inner->RecruitCostMultiplier(role); }
        f32 CaravanBonus() const override { return m_inner->CaravanBonus() + m_building.caravanBonus; }
        f32 ConversionCostMultiplier() const override { return m_inner->ConversionCostMultiplier(); }
        f32 DistancePenaltyMultiplier() const override { return m_inner->DistancePenaltyMultiplier(); }
        f32 MoraleBonus() const override { return m_inner->MoraleBonus() + m_building.moraleBonus; }
        f32 ForestHarvest() const override { return m_inner->ForestHarvest() + m_building.forestHarvest; }
        f32 FieldRadiusBonus() const override { return m_inner->FieldRadiusBonus() + m_building.fieldRadiusBonus; }
        f32 SiegeSupplyDays() const override { return m_inner->SiegeSupplyDays() + m_building.siegeSupplyDays; }

    protected:
        Scope<ISettlementEvaluator> m_inner;
        const BuildingInfo& m_building;
    };
}
