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
//
// A battle has two sides, not two armies. Any host that comes up while the fight is on
// joins whichever side it is friendly to and adds its weight to that side's line. Where it
// comes from matters as much as how heavy it is: a second host arriving on the enemy's
// flank shakes him, and one arriving at his back breaks him, because a line that has to
// turn round is not a line any more.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Json.h"
#include "../World/Cohort.h"

namespace woc
{
    class World;

    struct BattleSide
    {
        /// The host that began the fight on this side, and whose banner the side carries.
        EntityId cohort = kInvalidId;
        EntityId clan = kInvalidId;
        std::string name;
        u32 startingStrength = 0;
        u32 losses = 0;        // dead
        u32 hurt = 0;          // carried off, and coming back
        f32 power = 0.0f;
        bool routed = false;
        bool withdrew = false; // walked away on purpose rather than broke
        /// Hosts that have come up alongside since it started. They fight as one line.
        std::vector<EntityId> support;
        /// How badly this side is being taken from the flank or the rear, 0..1. It tells on
        /// morale rather than on damage: men break when they are hit from behind.
        f32 flanked = 0.0f;

        /// Every host on this side, the first one included.
        std::vector<EntityId> Hosts() const;
        bool Includes(EntityId cohortId) const;
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
        /// Every fight in progress, to the round - so a resynchronised or reloaded party
        /// carries on with the battles it had rather than starting them over.
        Json ToJson() const;
        void FromJson(const Json& node);
        const std::vector<BattleReport>& History() const { return m_history; }

        /// Distance at which hostile cohorts are considered to be in contact.
        f32 EngagementRadius() const { return m_engagementRadius; }

        /// Effective strength of one cohort against a given opposing composition and terrain.
        f32 EvaluatePower(const World& world, EntityId cohortId, EntityId enemyCohortId,
                          const Vec2& position) const;

        /// Total defensive strength of a settlement, garrison and walls included.
        f32 SettlementDefense(World& world, EntityId settlementId) const;

        /// The battle this host is in - as the side's first banner or as a host that came
        /// up alongside - or null. The panel and the map marker use it.
        const BattleReport* BattleOf(EntityId cohortId) const;
        /// Breaks off the fight and walks away. Costs order and invites a parting blow, but
        /// a host that runs early lives to fight again - which is the point of allowing it.
        bool Withdraw(World& world, EntityId cohortId);

        /// Brings the wounded back to the ranks and takes its toll on hosts in the field.
        /// Called once a day's worth of simulation, alongside the fighting.
        void TickRecovery(World& world, f32 days);

        /// Fills the gaps in a host quartered in a settlement out of the people living
        /// there. Men and silver both: a company is made good with recruits, not by magic.
        void ReinforceGarrisons(World& world, i32 days);

        /// Where a host standing at `from` can fall back to, with the enemy at `threat`.
        /// Returns false when there is nowhere - the sea at its back, the mountain at its
        /// shoulder, and the enemy everywhere else - which is what being surrounded means.
        /// A retreat is never allowed to pass behind the enemy, because a line does not
        /// escape by walking through the men in front of it.
        bool FindRetreat(const World& world, const Cohort& cohort, const Vec2& threat,
                         f32 distance, Vec2& out) const;

    private:
        BattleSystem() = default;
        ~BattleSystem() = default;

        void ResolveFieldBattles(World& world, f32 days);
        void ResolveSieges(World& world, f32 days);
        void ResolveRaids(World& world);

        void RunRounds(World& world, BattleReport& report, i32 rounds);
        /// Folds in every friendly host standing close enough to be part of this fight.
        void GatherSupport(World& world, BattleReport& report);
        /// Works out, for each side, how far round its line the enemy has got. A host due
        /// opposite counts for nothing; one at right angles counts for half; one directly
        /// behind counts for all of it.
        void MeasureFlanks(World& world, BattleReport& report);
        /// The whole side's weight of arms, measured against the enemy's leading host.
        f32 SidePower(const World& world, const BattleSide& side, const BattleSide& enemy) const;
        u32 SideStrength(const World& world, const BattleSide& side) const;
        /// Spreads a blow across every host on a side, in proportion to how much of the
        /// line each of them is holding.
        void SpreadDamage(World& world, BattleSide& side, f32 damage, u32& deadOut, u32& hurtOut,
                          f32 killShare);
        /// Deals `damage` to a host. Casualties are split between the dead and the hurt;
        /// `killShare` is how merciless the blow is - a pursuit kills far more than a
        /// push of pike does.
        void ApplyDamage(World& world, Cohort& target, f32 damage, u32& deadOut, u32& hurtOut,
                         f32 killShare = 0.55f);
        /// What a host loses to hunger, exhaustion and the weather in a day afield.
        void ApplyAttrition(World& world, f32 days);
        void CaptureSettlement(World& world, EntityId settlementId, EntityId newOwner);
        /// Pulls a beaten host back out of contact, or destroys it where it stands when
        /// there is nowhere for it to go.
        void FallBack(World& world, Cohort& host, const Vec2& threat, f32 distance);
        /// Sends a host running for `landing`: a real march at retreat pace, not a jump, and
        /// kept out of contact for as long as the run takes.
        void BeginRetreat(World& world, Cohort& host, const Vec2& landing);
        /// Whether a host that has just broken is finished rather than merely beaten: too few
        /// men, no order left from an earlier rout, or a defeat so one-sided - odds, flanks,
        /// horsemen at its heels - that nothing gets away.
        bool IsAnnihilated(World& world, const Cohort& host, f32 organisationBefore, f32 decisiveness) const;
        /// Wipes out hosts that are past fighting: too few men left and no order to hold
        /// them together. Such a host does not withdraw in good order, it ceases to exist.
        void CullBrokenHosts(World& world, BattleReport& report);

        f32 m_engagementRadius = 18.0f;
        /// Days of simulation still owed to the recovery pass, so it runs on the day and
        /// not on the frame.
        f32 m_recoveryCarry = 0.0f;
        std::vector<BattleReport> m_active;
        std::vector<BattleReport> m_history;
    };
}
