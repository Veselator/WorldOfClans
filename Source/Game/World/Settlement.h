// Settlement.h - a village, city or castle on the map.
//
// A settlement is the unit of territory: it produces, it holds people, and its coverage
// field is what actually draws the realm's borders.
#pragma once

#include "SettlementDatabase.h"
#include "BuildingDatabase.h"
#include "UnitDatabase.h"
#include "EntityJson.h"
#include "../../Core/Math.h"

namespace woc
{
    struct ConstructionOrder
    {
        std::string buildingId;
        i32 daysRemaining = 0;
    };

    /// A company paid for and being mustered. Only the first order in a settlement's queue
    /// makes progress: a town raises one company at a time.
    struct RecruitOrder
    {
        UnitRole role = UnitRole::Swordsman;
        EntityId cohort = kInvalidId;   // where it goes when it is ready; a new garrison if gone
        u32 headCount = 0;
        f32 hoursTotal = 1.0f;
        f32 hoursLeft = 1.0f;

        f32 Progress() const { return hoursTotal > 0.0f ? Clamp01(1.0f - hoursLeft / hoursTotal) : 1.0f; }
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
        /// Drawn mirrored left to right. Rolled when the place first appears on the map, so
        /// a countryside of identical hamlets does not look stamped out of one mould.
        bool mirrored = false;

        std::vector<std::string> buildings;
        std::vector<ConstructionOrder> construction;
        std::vector<RecruitOrder> recruitQueue;

        // --- transient state, recomputed by the systems ---------------------------------
        f32 coverageStrength = 0.0f;      // effective coverage after buildings and race
        f32 lastProduction = 0.0f;        // units of the base resource per month
        f32 siegeProgress = 0.0f;         // 0..1, set by BattleSystem
        EntityId besiegedBy = kInvalidId;
        i32 conversionDaysLeft = 0;
        i32 rebelliousUntilDay = 0;   // freshly revolted: coverage alone will not retake it
        /// Until this day the place is still getting used to a new lord. Conquest, however
        /// welcome, unsettles a town; the unrest is real but it passes, and it is the only
        /// reason a place of the lord's own people and gods is ever discontented.
        i32 newLordUntilDay = 0;
        /// Days of building work still owed before the seat is a seat. Nobody raises a city
        /// in an afternoon: until this runs out the place stands on the map as a site, gives
        /// nothing, holds no country and takes no orders.
        f32 foundingDaysLeft = 0.0f;
        f32 foundingDaysTotal = 0.0f;
        /// Taken on nobody's ground, and therefore held by nothing but the fact of standing
        /// there. Should another realm's authority close over the place, it changes hands
        /// without a blow - which is what happens to a garrison deep inside someone else's
        /// country. A seat founded or taken on one's own land never carries this.
        bool heldByPresence = false;
        std::string conversionTarget;
        bool quarryRevealed = false;

        // --- queries ---------------------------------------------------------------------
        const SettlementKindInfo& KindInfo() const { return SettlementDatabase::Get().Kind(kind); }
        const SettlementTier& Tier() const;
        size_t TierIndex() const { return KindInfo().TierForPopulation(population); }
        std::string TierName() const;

        bool IsIndependent() const { return owner == kInvalidId; }
        /// Still being built: on the map, but not yet a working settlement.
        bool UnderConstruction() const { return foundingDaysLeft > 0.0f; }
        f32 FoundingProgress() const
        {
            return foundingDaysTotal > 0.0f ? 1.0f - foundingDaysLeft / foundingDaysTotal : 1.0f;
        }
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
