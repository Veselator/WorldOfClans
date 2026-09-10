// Clan.h - a noble house: its lands, its treasury, its armies and its bloodline.
#pragma once

#include "ResourceData.h"
#include "EntityJson.h"
#include "../../Core/Math.h"

namespace woc
{
    class Clan
    {
    public:
        EntityId id = kInvalidId;
        EntityId state = kInvalidId;      // the realm this clan belongs to

        std::string name;
        std::string raceId = "human";
        std::string faithId = "perun";
        Color color{ 0.8f, 0.3f, 0.2f, 1.0f };
        u8 paletteSlot = 1;               // index into the renderer's owner palette

        ResourceData resources;

        std::vector<EntityId> settlements;
        std::vector<EntityId> cohorts;

        // --- dynasty -----------------------------------------------------------------------
        EntityId head = kInvalidId;       // the current lord
        std::vector<EntityId> members;    // every living noble of the house

        f32 prestige = 0.0f;
        bool eliminated = false;

        // --- convenience --------------------------------------------------------------------
        bool OwnsSettlement(EntityId settlement) const;
        void AddSettlement(EntityId settlement);
        void RemoveSettlement(EntityId settlement);
        void AddCohort(EntityId cohort);
        void RemoveCohort(EntityId cohort);

        Json ToJson() const;
        static Clan FromJson(const Json& node);
    };
}
