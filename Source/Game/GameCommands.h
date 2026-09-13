// GameCommands.h - every order a player can give, in a form that can be written down.
//
// Single player, this is a formality: the command is built and immediately run. In a
// multiplayer party it is the whole point. Every machine runs the same simulation, so no
// machine may change the world on its own: an order is written down, handed to the host,
// put on a tick, and carried out at that tick on every machine at once - the host's own
// orders included. That is deterministic lockstep, and it is why the world does not have
// to be sent anywhere.
#pragma once

#include "../Core/Json.h"
#include "../Core/Math.h"
#include "World/Cohort.h"
#include "World/SettlementDatabase.h"
#include "World/UnitDatabase.h"

namespace woc
{
    class World;

    class GameCommands
    {
    public:
        /// Runs the order here in a single party. In a multiplayer party it is submitted to
        /// be put on a tick, and nothing happens until that tick: returns false then.
        static bool Issue(World& world, const Json& command);

        /// Runs an order that arrived from a player. `peerId` is who sent it, as the host
        /// observed it rather than as the sender claimed it, and an order that does not
        /// concern that player's realm is dropped.
        static bool Execute(World& world, const Json& command, const std::string& peerId);

        // --- the orders ------------------------------------------------------------------
        static Json Task(EntityId cohort, TaskType type, const Vec2& destination,
                         EntityId settlement = kInvalidId, EntityId targetCohort = kInvalidId);
        static Json Stop(EntityId cohort);
        /// A diplomatic act of the sender's realm towards another.
        static Json Diplomacy(EntityId targetState, i32 actionKind);
        /// The sender's answer to the first embassy waiting on his realm.
        static Json AnswerOffer(bool accept);
        static Json Withdraw(EntityId cohort);
        static Json Disband(EntityId cohort);
        static Json Split(EntityId cohort, const std::vector<EntityId>& units);
        static Json SplitInHalf(EntityId cohort);
        static Json Merge(const std::vector<EntityId>& cohorts);
        static Json SetRaiding(EntityId cohort, bool value);
        static Json SetSuppressing(EntityId cohort, bool value);

        static Json Build(EntityId settlement, const std::string& building);
        static Json CancelBuild(EntityId settlement, const std::string& building);
        static Json CancelRecruit(EntityId settlement, i32 index);
        static Json Recruit(EntityId settlement, EntityId cohort, UnitRole role);
        static Json Road(EntityId from, EntityId to);
        static Json Convert(EntityId settlement, const std::string& faith);
        static Json Raze(EntityId settlement);
        static Json Independence(EntityId settlement);
        static Json DevelopMine(EntityId mine);
        static Json Found(SettlementKind kind, const Vec2& position, const std::string& name);
        static Json PlantForest(const Vec2& position);
        static Json Trade(EntityId settlement, i32 give, i32 take, f32 amount);

    private:
        /// Which realm an order concerns, so the host can refuse one that concerns another.
        static EntityId ClanOf(World& world, const Json& command);
    };
}
