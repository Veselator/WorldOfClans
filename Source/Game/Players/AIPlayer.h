// AIPlayer.h - the opposing lords.
//
// The AI scores a handful of concrete intents each time it thinks - build, recruit,
// expand, defend, attack - and commits to the best one it can currently afford. It uses
// the same commands the player's interface does, so it can never do anything you cannot.
#pragma once

#include "IPlayer.h"
#include "../../Core/Math.h"
#include "../../Core/Random.h"

namespace woc
{
    class World;
    class Clan;

    class AIPlayer final : public IPlayer
    {
    public:
        AIPlayer(EntityId stateId, u32 seed) : IPlayer(stateId), m_random(seed) {}

        bool IsHuman() const override { return false; }
        const char* Kind() const override { return "ai"; }

        void OnThink(World& world, i32 day) override;

    private:
        void ManageEconomy(World& world, Clan& clan);
        void ManageMilitary(World& world, Clan& clan);
        void ManageExpansion(World& world, Clan& clan);

        /// Chooses a target for an idle army: the most valuable reachable enemy holding.
        EntityId PickOffensiveTarget(World& world, const Clan& clan, const Vec2& from) const;
        /// The settlement most in need of a garrison right now.
        EntityId PickThreatenedSettlement(World& world, const Clan& clan) const;

        Random m_random;
        i32 m_lastExpansionDay = -9999;
    };
}
