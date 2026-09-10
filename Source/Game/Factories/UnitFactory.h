// UnitFactory.h - builds units and cohorts out of named people.
#pragma once

#include "../World/Unit.h"
#include "../World/Cohort.h"
#include "../../Core/Random.h"

namespace woc
{
    class World;
    class Settlement;

    class UnitFactory
    {
    public:
        /// Creates a unit of `headCount` people and attaches it to `cohortId`.
        /// Aristocrat units are always a single person, whatever is asked for.
        static Unit& Create(World& world, EntityId cohortId, const std::string& raceId, UnitRole role,
                            u32 headCount, const std::string& origin, Random& random);

        /// Creates an empty cohort owned by `clanId`, standing at `position`.
        static Cohort& CreateCohort(World& world, EntityId clanId, const Vec2& position, Random& random,
                                    const std::string& name = "");

        /// Convenience used by world generation and by the AI: a balanced starting force.
        static Cohort& CreateRetinue(World& world, EntityId clanId, const Settlement& home,
                                     u32 unitCount, Random& random);

        /// Head count a unit raised in this settlement would have.
        static u32 EstablishmentFor(const Settlement& settlement);
    };
}
