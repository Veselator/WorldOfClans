// AIPlayer.h - the opposing lords.
//
// The AI scores a handful of concrete intents each time it thinks - build, recruit,
// expand, defend, attack - and commits to the best one it can currently afford. It uses
// the same commands the player's interface does, so it can never do anything you cannot.
//
// Two things shape every decision. The first is its temperament (see AIProfile): the same
// code produces a prince who hides behind his walls and one who campaigns abroad. The
// second is its own border: targets and rally points are measured against the shape of
// the realm, so an army is never sent to the far side of the map for a village.
#pragma once

#include "IPlayer.h"
#include "AIProfile.h"
#include "../World/ResourceData.h"
#include "../../Core/Math.h"
#include "../../Core/Random.h"

namespace woc
{
    class World;
    class Clan;
    class Cohort;

    class AIPlayer final : public IPlayer
    {
    public:
        AIPlayer(EntityId stateId, u32 seed);

        bool IsHuman() const override { return false; }
        const char* Kind() const override { return m_profile.id.c_str(); }

        const AIProfile& Profile() const { return m_profile; }

    public:
        void OnThink(World& world, i32 day) override;

    private:
        /// The shape of a realm: where its weight lies and how far it reaches.
        struct RealmShape
        {
            Vec2 center;
            f32 radius = 0.0f;
            bool valid = false;
        };

        void ManageEconomy(World& world, Clan& clan);
        /// How badly the realm wants more of a resource, as a multiplier on a building's
        /// score. Comparing raw output figures across resources is meaningless - three
        /// stone is not three silver - and this is what makes them comparable.
        f32 ScarcityWeight(World& world, const Clan& clan, ResourceType resource) const;
        void ConnectHoldings(World& world, Clan& clan);
        void ManageMilitary(World& world, Clan& clan, const RealmShape& realm);
        void ManageExpansion(World& world, Clan& clan);
        void ManageDiplomacy(World& world, const Clan& clan, const RealmShape& realm);

        static RealmShape MeasureRealm(const World& world, const Clan& clan);
        /// True while `position` is inside the realm plus whatever the temperament allows.
        bool WithinReach(const RealmShape& realm, const Vec2& position) const;

        /// Chooses a target for an idle army: the most valuable enemy holding within reach.
        EntityId PickOffensiveTarget(World& world, const Clan& clan, const RealmShape& realm,
                                     const Vec2& from) const;
        /// The settlement most in need of a garrison right now.
        EntityId PickThreatenedSettlement(World& world, const Clan& clan) const;
        /// The own holding nearest to hostile ground: where idle armies gather.
        EntityId PickRallyPoint(World& world, const Clan& clan) const;
        /// Sends an army to stand near the rally seat rather than disappear inside it.
        void Muster(World& world, Cohort& cohort, EntityId rally) const;
        /// Total fighting strength of a realm, for judging whether a war is worth it.
        static f32 StateStrength(const World& world, EntityId stateId);

        AIProfile m_profile;
        Random m_random;
        i32 m_lastExpansionDay = -9999;
        i32 m_lastWarDay = -9999;
    };
}
