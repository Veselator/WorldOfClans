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
        /// Outlaws. They hold no land, project no authority, sit at nobody's table and are
        /// at war with everybody - including other bands of outlaws. Everything that asks
        /// "which realms are there" has to skip them, because they are not one.
        bool outlaw = false;
        /// In a multiplayer party, which player holds this realm. Empty for the machine's
        /// own realms and for every realm in a single party. The host checks an incoming
        /// order against this and against nothing else.
        std::string peerId;

        std::unordered_map<EntityId, Relation> relations;
        /// Realms this one has laid eyes on - a column of theirs, or one of their towns.
        /// Under the fog of war nobody treats with a realm it has never seen. Symmetric.
        std::vector<EntityId> met;
        bool HasMet(EntityId other) const;

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
