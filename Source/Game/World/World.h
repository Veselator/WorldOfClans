// World.h - the live game state and the registry every system talks to.
//
// Entities live in id-keyed maps so references stay valid while containers grow, and so
// a save file is a straightforward dump of those maps.
#pragma once

#include "Character.h"
#include "Clan.h"
#include "Cohort.h"
#include "GameTime.h"
#include "Settlement.h"
#include "State.h"
#include "Unit.h"
#include "../Map/MapData.h"
#include "../Map/MapLoader.h"
#include "../Players/IPlayer.h"
#include "../../Core/Singleton.h"

#include <unordered_map>

namespace woc
{
    /// A quarry or ore site discovered on the map; developing it needs a nearby settlement.
    struct MineSite
    {
        EntityId id = kInvalidId;
        Vec2 position;
        ResourceType resource = ResourceType::Stone;
        f32 richness = 1.0f;
        EntityId owner = kInvalidId;
        bool developed = false;
    };

    struct RoadSegment
    {
        std::vector<Vec2> points;
        i32 level = 1;
    };

    /// A line in the chronicle shown to the player.
    struct Chronicle
    {
        i32 day = 0;
        std::string text;
        Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    class World final : public Singleton<World>
    {
        friend class Singleton<World>;
    public:
        // --- lifecycle ---------------------------------------------------------------------
        void Reset();
        bool LoadMap(const std::string& folder);
        const MapData& Map() const { return m_map; }
        MapData& MutableMap() { return m_map; }
        const MapDescription& MapInfo() const { return m_mapInfo; }

        GameTime& Time() { return m_time; }
        const GameTime& Time() const { return m_time; }

        // --- entity creation -----------------------------------------------------------------
        Character& CreateCharacter();
        Unit& CreateUnit();
        Cohort& CreateCohort();
        Settlement& CreateSettlement();
        Clan& CreateClan();
        State& CreateState();
        MineSite& CreateMine();

        // --- lookup ----------------------------------------------------------------------------
        Character* FindCharacter(EntityId id);
        Unit* FindUnit(EntityId id);
        Cohort* FindCohort(EntityId id);
        Settlement* FindSettlement(EntityId id);
        Clan* FindClan(EntityId id);
        State* FindState(EntityId id);
        MineSite* FindMine(EntityId id);

        const Character* FindCharacter(EntityId id) const;
        const Unit* FindUnit(EntityId id) const;
        const Cohort* FindCohort(EntityId id) const;
        const Settlement* FindSettlement(EntityId id) const;
        const Clan* FindClan(EntityId id) const;
        const State* FindState(EntityId id) const;

        std::unordered_map<EntityId, Character>& Characters() { return m_characters; }
        std::unordered_map<EntityId, Unit>& Units() { return m_units; }
        std::unordered_map<EntityId, Cohort>& Cohorts() { return m_cohorts; }
        std::unordered_map<EntityId, Settlement>& Settlements() { return m_settlements; }
        std::unordered_map<EntityId, Clan>& Clans() { return m_clans; }
        std::unordered_map<EntityId, State>& States() { return m_states; }
        std::vector<MineSite>& Mines() { return m_mines; }
        std::vector<RoadSegment>& Roads() { return m_roads; }

        const std::unordered_map<EntityId, Settlement>& Settlements() const { return m_settlements; }
        const std::unordered_map<EntityId, Cohort>& Cohorts() const { return m_cohorts; }
        const std::unordered_map<EntityId, Clan>& Clans() const { return m_clans; }
        const std::unordered_map<EntityId, State>& States() const { return m_states; }
        const std::vector<MineSite>& Mines() const { return m_mines; }
        const std::vector<RoadSegment>& Roads() const { return m_roads; }

        // --- destruction -------------------------------------------------------------------------
        void DestroyCohort(EntityId id);
        void DestroyUnit(EntityId id);
        void DestroySettlement(EntityId id);

        // --- relationships --------------------------------------------------------------------------
        State* StateOfClan(EntityId clanId);
        const State* StateOfClan(EntityId clanId) const;
        Clan* ClanOfSettlement(EntityId settlementId);
        Clan* ClanOfCohort(EntityId cohortId);
        bool AreHostile(EntityId clanA, EntityId clanB) const;

        /// Total head count of a cohort across all of its units.
        u32 CohortStrength(EntityId cohortId) const;
        /// Aggregate offensive power, used by the AI and the battle system.
        f32 CohortPower(EntityId cohortId) const;

        Settlement* NearestSettlement(const Vec2& position, f32 maxDistance = 1e9f, EntityId ownerFilter = kInvalidId);
        std::vector<EntityId> SettlementsWithin(const Vec2& position, f32 radius) const;
        std::vector<EntityId> CohortsWithin(const Vec2& position, f32 radius) const;

        // --- the player seats ---------------------------------------------------------------------------
        void AddPlayer(Scope<IPlayer> player);
        const std::vector<Scope<IPlayer>>& Players() const { return m_players; }
        IPlayer* PlayerForState(EntityId stateId);
        EntityId HumanStateId() const { return m_humanState; }
        void SetHumanState(EntityId stateId) { m_humanState = stateId; }
        Clan* HumanClan();
        State* HumanState() { return FindState(m_humanState); }

        // --- chronicle ---------------------------------------------------------------------------------
        void Log(const std::string& text, const Color& color);
        const std::vector<Chronicle>& ChronicleEntries() const { return m_chronicle; }

        // --- persistence ---------------------------------------------------------------------------------
        Json SaveObjects() const;
        /// Restores a saved world. With `includeRealms` false only the map's furniture is
        /// read - roads, mines and the forest and field layers - so a new party can seed
        /// its own realms onto an authored landscape.
        void LoadObjects(const Json& node, bool includeRealms = true);

        u32 Seed() const { return m_seed; }
        void SetSeed(u32 seed) { m_seed = seed; }

    private:
        World() = default;
        ~World() = default;

        EntityId NextId() { return m_nextId++; }

        MapData m_map;
        MapDescription m_mapInfo;
        GameTime m_time;

        std::unordered_map<EntityId, Character> m_characters;
        std::unordered_map<EntityId, Unit> m_units;
        std::unordered_map<EntityId, Cohort> m_cohorts;
        std::unordered_map<EntityId, Settlement> m_settlements;
        std::unordered_map<EntityId, Clan> m_clans;
        std::unordered_map<EntityId, State> m_states;
        std::vector<MineSite> m_mines;
        std::vector<RoadSegment> m_roads;

        std::vector<Scope<IPlayer>> m_players;
        std::vector<Chronicle> m_chronicle;

        EntityId m_humanState = kInvalidId;
        EntityId m_nextId = 1;
        u32 m_seed = 1337;
    };
}
