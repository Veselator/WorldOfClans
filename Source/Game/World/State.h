// State.h - a realm: several clans, one of which leads.
//
// Diplomacy happens between states, not clans; a clan changes its allegiance by moving
// between states, which is how a successful vassal becomes an independent power.
#pragma once

#include "EntityJson.h"
#include "../../Core/Math.h"
#include <unordered_map>

namespace woc
{
    enum class DiplomaticStance : u8
    {
        Neutral = 0,
        NonAggression,
        Alliance,
        War,
        Truce
    };

    struct Relation
    {
        DiplomaticStance stance = DiplomaticStance::Neutral;
        f32 opinion = 0.0f;         // -100..100
        i32 stanceUntilDay = 0;     // truces expire
    };

    class State
    {
    public:
        EntityId id = kInvalidId;

        std::string name;
        std::string raceId = "human";
        Color color{ 0.8f, 0.3f, 0.2f, 1.0f };

        std::vector<EntityId> clans;   // "Clan[] _clans"
        EntityId leader = kInvalidId;  // "Clan _leader" - the ruling house

        bool playerControlled = false;
        bool eliminated = false;

        std::unordered_map<EntityId, Relation> relations;

        Relation& RelationWith(EntityId other);
        const Relation& RelationWith(EntityId other) const;
        DiplomaticStance StanceWith(EntityId other) const;
        bool IsAtWarWith(EntityId other) const { return StanceWith(other) == DiplomaticStance::War; }
        bool IsAlliedWith(EntityId other) const { return StanceWith(other) == DiplomaticStance::Alliance; }

        void AddClan(EntityId clan);
        void RemoveClan(EntityId clan);

        static const char* StanceName(DiplomaticStance stance);

        Json ToJson() const;
        static State FromJson(const Json& node);
    };
}
