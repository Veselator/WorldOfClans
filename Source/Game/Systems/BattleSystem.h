// BattleSystem.h - field battles, sieges, raids.
//
// A round of combat weighs, in order: raw numbers and stats, the rock-paper-scissors
// matrix between the arms present, terrain affinity, the height advantage, training and
// experience, and finally morale - which is what actually ends most battles.
//
// A battle is not an event but a state. Two hosts in contact stay locked together for as
// long as it takes, trading a few rounds a day, and either side may break off at any
// moment - the player by ordering it, the AI when its nerve goes. Losing the field costs
// a host the ground and most of its order; it does not, as a rule, cost it its existence.
//
// Casualties are not all dead. A share of every blow is men carried off the field, who
// return to the ranks over the following weeks - which is why a bloodied army is worth
// resting rather than disbanding.
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
        u32 losses = 0;        // dead
        u32 hurt = 0;          // carried off, and coming back
        f32 power = 0.0f;
        bool routed = false;
        bool withdrew = false; // walked away on purpose rather than broke
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

        /// Fractions of a round carried between ticks, so the pace of a battle follows the
        /// game clock rather than the frame rate.
        f32 roundProgress = 0.0f;
        /// How long the two have been at it, in days. The interface shows it and the AI
        /// uses it to decide whether this is going anywhere.
        f32 elapsedDays = 0.0f;
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

        /// The battle this host is in, or null. The panel and the map marker use it.
        const BattleReport* BattleOf(EntityId cohortId) const;
        /// Breaks off the fight and walks away. Costs order and invites a parting blow, but
        /// a host that runs early lives to fight again - which is the point of allowing it.
        bool Withdraw(World& world, EntityId cohortId);

        /// Brings the wounded back to the ranks and takes its toll on hosts in the field.
        /// Called once a day's worth of simulation, alongside the fighting.
        void TickRecovery(World& world, f32 days);

    private:
        BattleSystem() = default;
        ~BattleSystem() = default;

        void ResolveFieldBattles(World& world, f32 days);
        void ResolveSieges(World& world, f32 days);
        void ResolveRaids(World& world);

        void RunRounds(World& world, BattleReport& report, i32 rounds);
        /// Deals `damage` to a host. Casualties are split between the dead and the hurt;
        /// `killShare` is how merciless the blow is - a pursuit kills far more than a
        /// push of pike does.
        void ApplyDamage(World& world, Cohort& target, f32 damage, u32& deadOut, u32& hurtOut,
                         f32 killShare = 0.55f);
        /// What a host loses to hunger, exhaustion and the weather in a day afield.
        void ApplyAttrition(World& world, f32 days);
        void CaptureSettlement(World& world, EntityId settlementId, EntityId newOwner);

        f32 m_engagementRadius = 18.0f;
        /// Days of simulation still owed to the recovery pass, so it runs on the day and
        /// not on the frame.
        f32 m_recoveryCarry = 0.0f;
        std::vector<BattleReport> m_active;
        std::vector<BattleReport> m_history;
    };
}
