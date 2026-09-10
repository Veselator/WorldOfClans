// SettlementSystem.h - construction, conversion and the orders a lord gives his towns.
//
// Everything here is a command the player issues through the UI and the AI issues through
// its evaluation, so both go down exactly the same code path.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/Settlement.h"
#include "../World/UnitDatabase.h"

namespace woc
{
    class World;

    struct BuildOption
    {
        const BuildingInfo* building = nullptr;
        bool affordable = false;
        bool allowed = false;
        std::string blockedReason;
    };

    struct RecruitOption
    {
        UnitRole role = UnitRole::Swordsman;
        std::string name;
        u32 headCount = 0;
        f32 cost = 0.0f;
        bool affordable = false;
        std::string blockedReason;
    };

    class SettlementSystem final : public Singleton<SettlementSystem>
    {
        friend class Singleton<SettlementSystem>;
    public:
        /// Advances construction queues and religious conversions by `days`.
        void Tick(World& world, i32 days);

        // --- queries the interface uses -------------------------------------------------------
        std::vector<BuildOption> BuildOptions(World& world, EntityId settlementId) const;
        std::vector<RecruitOption> RecruitOptions(World& world, EntityId settlementId) const;
        f32 ConversionCost(World& world, EntityId settlementId, const std::string& faithId) const;

        // --- commands ---------------------------------------------------------------------------
        bool StartConstruction(World& world, EntityId settlementId, const std::string& buildingId);
        bool CancelConstruction(World& world, EntityId settlementId, const std::string& buildingId);
        /// Recruits a unit into `cohortId`, or into a new cohort when it is kInvalidId.
        EntityId Recruit(World& world, EntityId settlementId, EntityId cohortId, UnitRole role);
        bool StartConversion(World& world, EntityId settlementId, const std::string& faithId);
        bool Raze(World& world, EntityId settlementId, EntityId actingClan);
        bool GrantIndependence(World& world, EntityId settlementId);
        /// Founds a new settlement paid for by `clanId`.
        EntityId Found(World& world, EntityId clanId, SettlementKind kind, const Vec2& position);

    private:
        SettlementSystem() = default;
        ~SettlementSystem() = default;

        bool RequirementsMet(const World& world, const Settlement& settlement,
                             const BuildingInfo& building, std::string& reason) const;
    };
}
