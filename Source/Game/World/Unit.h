// Unit.h - one subdivision inside a cohort: between 1 and 100 named people.
#pragma once

#include "UnitDatabase.h"
#include "EntityJson.h"

namespace woc
{
    class World;

    class Unit
    {
    public:
        EntityId id = kInvalidId;
        EntityId cohort = kInvalidId;

        UnitRole role = UnitRole::Swordsman;
        std::string raceId = "human";
        std::string name;

        std::vector<EntityId> characters;   // the actual people, by id
        u32 establishment = 50;             // full-strength head count

        f32 morale = 0.75f;
        f32 training = 0.35f;
        f32 fatigue = 0.0f;

        const UnitData& Stats() const { return UnitDatabase::Get().Stats(raceId, role); }

        u32 Strength() const { return static_cast<u32>(characters.size()); }
        f32 StrengthFraction() const
        {
            return establishment > 0 ? static_cast<f32>(characters.size()) / establishment : 0.0f;
        }
        bool IsDestroyed() const { return characters.empty(); }

        /// Aggregate offensive power, before counters and terrain are applied.
        f32 CombatPower(f32 cohortExperience) const;
        /// Aggregate staying power.
        f32 DefensivePower(f32 cohortExperience) const;

        std::string DisplayName() const;

        Json ToJson() const;
        static Unit FromJson(const Json& node);
    };
}
