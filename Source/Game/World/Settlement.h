// Settlement.h - a village, city or castle on the map.
//
// A settlement is the unit of territory: it produces, it holds people, and its coverage
// field is what actually draws the realm's borders.
#pragma once

#include "SettlementDatabase.h"
#include "BuildingDatabase.h"
#include "EntityJson.h"
#include "../../Core/Math.h"

namespace woc
{
    struct ConstructionOrder
    {
        std::string buildingId;
        i32 daysRemaining = 0;
    };

    class Settlement
    {
    public:
        EntityId id = kInvalidId;
        EntityId owner = kInvalidId;      // kInvalidId = independent

        std::string name;
        SettlementKind kind = SettlementKind::Village;
        Vec2 position;

        std::string raceId = "human";     // never changes: you cannot convert a people
        std::string faithId = "perun";    // can be converted, at a price

        i32 population = 200;
        f32 prosperity = 0.5f;
        f32 loyalty = 0.75f;

        std::vector<std::string> buildings;
        std::vector<ConstructionOrder> construction;

        // --- transient state, recomputed by the systems ---------------------------------
        f32 coverageStrength = 0.0f;      // effective coverage after buildings and race
        f32 lastProduction = 0.0f;        // units of the base resource per month
        f32 siegeProgress = 0.0f;         // 0..1, set by BattleSystem
        EntityId besiegedBy = kInvalidId;
        i32 conversionDaysLeft = 0;
        i32 rebelliousUntilDay = 0;   // freshly revolted: coverage alone will not retake it
        std::string conversionTarget;
        bool quarryRevealed = false;

        // --- queries ---------------------------------------------------------------------
        const SettlementKindInfo& KindInfo() const { return SettlementDatabase::Get().Kind(kind); }
        const SettlementTier& Tier() const;
        size_t TierIndex() const { return KindInfo().TierForPopulation(population); }
        std::string TierName() const;

        bool IsIndependent() const { return owner == kInvalidId; }
        bool HasBuilding(const std::string& buildingId) const;
        bool IsBuilding(const std::string& buildingId) const;

        /// Base coverage from the tier table, before decorators.
        f32 BaseCoverage() const { return Tier().coverage; }
        SpriteId Sprite() const { return KindInfo().sprite; }

        Json ToJson() const;
        static Settlement FromJson(const Json& node);
    };
}
