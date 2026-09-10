// DynastySystem.h - ageing, births, deaths and succession.
//
// Nobles age on the game calendar, marry across realms, have children and eventually die;
// when a clan's head dies the eldest living child inherits, and a house with no heir left
// is absorbed by its realm.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/Character.h"

namespace woc
{
    class World;
    class Clan;

    class DynastySystem final : public Singleton<DynastySystem>
    {
        friend class Singleton<DynastySystem>;
    public:
        /// Runs once per simulated year.
        void Tick(World& world);

        /// Founds a ruling family for a clan that has none.
        void FoundDynasty(World& world, EntityId clanId, const std::string& seatName);

        /// The character who would inherit if the head died now.
        EntityId HeirOf(const World& world, EntityId clanId) const;

        /// Everyone in a character's immediate family, for the character panel.
        std::vector<EntityId> ImmediateFamily(const World& world, EntityId characterId) const;

    private:
        DynastySystem() = default;
        ~DynastySystem() = default;

        void AgeCharacters(World& world);
        void ResolveBirths(World& world);
        void ResolveDeaths(World& world);
        void Inherit(World& world, Clan& clan);
    };
}
