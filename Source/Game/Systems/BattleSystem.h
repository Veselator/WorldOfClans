// BattleSystem.h - field battles, sieges, raids.
//
// A round of combat weighs, in order: raw numbers and stats, the rock-paper-scissors
// matrix between the arms present, terrain affinity, the height advantage, training and
// experience, and finally morale - which is what actually ends most battles.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/Cohort.h"

namespace woc
{
    class World;

    struct BattleSide
    {
        EntityId cohort = kInvalidId;
        EntityId clan = kInvalidId;
        std::string name;
        u32 startingStrength = 0;
        u32 losses = 0;
        f32 power = 0.0f;
        bool routed = false;
    };

    struct BattleReport
    {
        i32 day = 0;
        Vec2 position;
        BattleSide attacker;
        BattleSide defender;
        std::string terrainName;
        bool concluded = false;
        EntityId victor = kInvalidId;
    };

    class BattleSystem final : public Singleton<BattleSystem>
    {
        friend class Singleton<BattleSystem>;
    public:
        void Tick(World& world, f32 days);

        const std::vector<BattleReport>& ActiveBattles() const { return m_active; }
        const std::vector<BattleReport>& History() const { return m_history; }

        /// Distance at which hostile cohorts are considered to be in contact.
        f32 EngagementRadius() const { return m_engagementRadius; }

        /// Effective strength of one cohort against a given opposing composition and terrain.
        f32 EvaluatePower(const World& world, EntityId cohortId, EntityId enemyCohortId,
                          const Vec2& position) const;

        /// Total defensive strength of a settlement, garrison and walls included.
        f32 SettlementDefense(World& world, EntityId settlementId) const;

    private:
        BattleSystem() = default;
        ~BattleSystem() = default;

        void ResolveFieldBattles(World& world, f32 days);
        void ResolveSieges(World& world, f32 days);
        void ResolveRaids(World& world);

        void RunRounds(World& world, BattleReport& report, i32 rounds);
        void ApplyDamage(World& world, Cohort& target, f32 damage, u32& lossesOut);
        void CaptureSettlement(World& world, EntityId settlementId, EntityId newOwner);

        f32 m_engagementRadius = 18.0f;
        std::vector<BattleReport> m_active;
        std::vector<BattleReport> m_history;
    };
}
