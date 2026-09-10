// CharacterFactory.h - creates people, from levy spearmen to dynasty founders.
#pragma once

#include "../World/Character.h"
#include "../../Core/Random.h"

namespace woc
{
    class World;

    class CharacterFactory
    {
    public:
        /// A rank-and-file soldier or townsman born in `origin`.
        static Character& CreateCommoner(World& world, const std::string& raceId,
                                         const std::string& origin, Random& random);

        /// A noble with a surname, a house and a place in the succession.
        static Character& CreateNoble(World& world, const std::string& raceId, EntityId clanId,
                                      const std::string& origin, Gender gender, i32 age, Random& random);

        /// A child of two nobles; inherits the father's house and surname.
        static Character& CreateChild(World& world, Character& father, Character& mother, Random& random);

    private:
        static void RollPhysique(Character& character, Random& random);
        static void RollTraits(Character& character, Random& random, int maximum);
    };
}
