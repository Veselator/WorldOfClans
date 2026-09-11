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
        /// Men carried on the strength but not in the line: cut about, and mending. They
        /// are the same people, kept by id, so a veteran who takes a spear in the shoulder
        /// comes back as himself and not as a replacement. Most return; some do not.
        std::vector<EntityId> wounded;
        u32 establishment = 50;             // full-strength head count

        f32 morale = 0.75f;
        f32 training = 0.35f;
        f32 fatigue = 0.0f;

        const UnitData& Stats() const { return UnitDatabase::Get().Stats(raceId, role); }

        u32 Strength() const { return static_cast<u32>(characters.size()); }
        u32 Wounded() const { return static_cast<u32>(wounded.size()); }
        /// Everyone the unit still has a claim on, standing or lying down.
        u32 Roll() const { return Strength() + Wounded(); }
        f32 StrengthFraction() const
        {
            return establishment > 0 ? static_cast<f32>(characters.size()) / establishment : 0.0f;
        }
        /// Out of the fight. A unit with nothing but wounded left is not destroyed - it
        /// still has men to get back - but it holds no part of the line.
        bool IsDestroyed() const { return characters.empty(); }
        bool IsGone() const { return characters.empty() && wounded.empty(); }

        /// Aggregate offensive power, before counters and terrain are applied.
        f32 CombatPower(f32 cohortExperience) const;
        /// Aggregate staying power.
        f32 DefensivePower(f32 cohortExperience) const;

        std::string DisplayName() const;

        Json ToJson() const;
        static Unit FromJson(const Json& node);
    };
}
