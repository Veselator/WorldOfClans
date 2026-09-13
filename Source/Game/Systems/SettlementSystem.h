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
        /// Pays for a company and puts it in the settlement's muster queue, bound for
        /// `cohortId` (or the garrison, or a new one). Returns the settlement on success.
        EntityId Recruit(World& world, EntityId settlementId, EntityId cohortId, UnitRole role);
        /// Takes an order out of the muster queue; the men go home and half the silver back.
        bool CancelRecruit(World& world, EntityId settlementId, size_t index);
        /// Moves every muster on by a fraction of a day - companies take hours, so they
        /// cannot wait for the daily pass.
        void TickMusters(World& world, f32 days);
        bool StartConversion(World& world, EntityId settlementId, const std::string& faithId);
        bool Raze(World& world, EntityId settlementId, EntityId actingClan);
        bool GrantIndependence(World& world, EntityId settlementId);

        /// What it costs this clan to open the quarry, and whether it may.
        struct MineOffer
        {
            bool allowed = false;
            bool affordable = false;
            std::string blockedReason;
            ResourceData cost;
            f32 stonePerMonth = 0.0f;
            i32 days = 0;
        };
        MineOffer MineOptions(World& world, EntityId clanId, EntityId mineId) const;
        /// How long opening a quarry takes. The same figure as the quarry a town builds
        /// inside its own walls, because it is the same work.
        i32 MineDevelopDays() const;
        /// Advances every quarry being dug. Called with the settlements' own construction.
        void TickMines(World& world, f32 days);
        /// Opens the quarry for `clanId`: charges the cost and puts it to work.
        bool DevelopMine(World& world, EntityId clanId, EntityId mineId);
        /// Founds a new settlement paid for by `clanId`. An empty name draws one from the
        /// pool. The settlers are taken from the clan's nearby holdings, not invented.
        EntityId Found(World& world, EntityId clanId, SettlementKind kind, const Vec2& position,
                       const std::string& name = std::string());

    private:
        /// Advances the first order in the queue and stands the company up when it is done.
        void MusterRecruits(World& world, Settlement& settlement, f32 days);
        SettlementSystem() = default;
        ~SettlementSystem() = default;

        bool RequirementsMet(const World& world, const Settlement& settlement,
                             const BuildingInfo& building, std::string& reason) const;
    };
}
