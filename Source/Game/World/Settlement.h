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
        /// Absolute wealth in grivnas, not a ratio. A hamlet sits near 60, a capital near 700;
        /// the tier's ceiling is what turns it back into a fraction where the formulas want one.
        f32 prosperity = 60.0f;
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
        /// What this tier can sustain, and how close the settlement is to it.
        f32 ProsperityCeiling() const;
        f32 ProsperityFraction() const;
        bool HasBuilding(const std::string& buildingId) const;
        bool IsBuilding(const std::string& buildingId) const;

        /// Base coverage from the tier table, before decorators.
        f32 BaseCoverage() const { return Tier().coverage; }
        /// The picture this settlement shows on the map: its tier's, if that tier has one
        /// of its own, and the kind's otherwise.
        SpriteId Sprite() const
        {
            const SpriteId tierSprite = Tier().sprite;
            return tierSprite == SpriteId::Count ? KindInfo().sprite : tierSprite;
        }

        Json ToJson() const;
        static Settlement FromJson(const Json& node);
    };
}
