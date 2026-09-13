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

        /// Opening a quarry on the map is the same work as raising one inside a town, so it
        /// takes the same time. While these are positive the site is claimed and being dug,
        /// and produces nothing.
        f32 daysRemaining = 0.0f;
        f32 daysTotal = 0.0f;

        bool UnderWay() const { return !developed && daysRemaining > 0.0f; }
        f32 Progress() const
        {
            return daysTotal > 0.0f ? 1.0f - daysRemaining / daysTotal : 0.0f;
        }
    };

    /// A robbers' camp: a few tents in country nobody rides through, and whatever they
    /// have taken buried under the floor of one of them.
    ///
    /// It is deliberately not a settlement. It holds no land, produces nothing, cannot be
    /// captured and does not appear in anybody's realm - the only things that can happen to
    /// it are that it sends out another band, or that somebody burns it down.
    struct BanditCamp
    {
        EntityId id = kInvalidId;
        EntityId clan = kInvalidId;      // which band of outlaws keeps it
        Vec2 position;
        std::string raceId = "human";

        /// What is buried under the floor. It grows as the bands bring plunder home, and
        /// whoever burns the camp carries it off.
        ResourceData hoard;
        /// How hard the camp is to storm, and how much of that is left after an assault.
        f32 strength = 1.0f;
        f32 damage = 0.0f;
        /// Days until another band walks out of it.
        f32 musterDays = 0.0f;

        f32 Health() const { return strength > 0.0f ? Clamp01(1.0f - damage / strength) : 0.0f; }
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

    /// A diplomatic event drawn on the map for a moment: a line struck between two
    /// capitals. Red for a war declared, green for a wedding, blue for an alliance.
    /// A line of news shouted across the top of the screen. The chronicle in the corner is
    /// where everything is written down; this is for the handful of things a player must
    /// not be allowed to scroll past - a war declared on him, above all.
    struct Herald
    {
        std::string headline;
        std::string detail;
        Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        f32 life = 0.0f;        // seconds of real time left
        f32 duration = 1.0f;
    };

    /// A rising, thrown up over the village that rose: a fist that swells out of nothing,
    /// overshoots, and settles. Purely a signal to the player.
    struct RevoltMark
    {
        Vec2 position;
        f32 life = 0.0f;        // seconds of real time left
        f32 duration = 1.0f;
    };

    struct DiplomaticFlare
    {
        Vec2 from;
        Vec2 to;
        Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        f32 life = 0.0f;        // seconds of real time left
        f32 duration = 1.0f;
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
        BanditCamp& CreateBanditCamp();

        // --- lookup ----------------------------------------------------------------------------
        Character* FindCharacter(EntityId id);
        Unit* FindUnit(EntityId id);
        Cohort* FindCohort(EntityId id);
        Settlement* FindSettlement(EntityId id);
        Clan* FindClan(EntityId id);
        State* FindState(EntityId id);
        MineSite* FindMine(EntityId id);
        const MineSite* FindMine(EntityId id) const;
        BanditCamp* FindBanditCamp(EntityId id);
        const BanditCamp* FindBanditCamp(EntityId id) const;
        void DestroyBanditCamp(EntityId id);

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
        std::vector<BanditCamp>& BanditCamps() { return m_banditCamps; }
        std::vector<RoadSegment>& Roads() { return m_roads; }

        const std::unordered_map<EntityId, Settlement>& Settlements() const { return m_settlements; }
        const std::unordered_map<EntityId, Cohort>& Cohorts() const { return m_cohorts; }
        const std::unordered_map<EntityId, Clan>& Clans() const { return m_clans; }
        const std::unordered_map<EntityId, State>& States() const { return m_states; }
        const std::vector<MineSite>& Mines() const { return m_mines; }
        const std::vector<BanditCamp>& BanditCamps() const { return m_banditCamps; }
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
        /// May this clan lay hands on that settlement at all? Only in open war - an
        /// unclaimed holding answers to nobody and so is fair game to everyone.
        bool MayAttackSettlement(EntityId clanId, EntityId settlementId) const;

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
        void ClearPlayers() { m_players.clear(); }
        IPlayer* PlayerForState(EntityId stateId);
        EntityId HumanStateId() const { return m_humanState; }
        void SetHumanState(EntityId stateId) { m_humanState = stateId; }
        Clan* HumanClan();
        State* HumanState() { return FindState(m_humanState); }

        // --- chronicle ---------------------------------------------------------------------------------
        void Log(const std::string& text, const Color& color);
        /// Strikes a line between two realms' capitals. Purely a signal to the player;
        /// nothing in the simulation reads it back.
        void Flare(EntityId stateA, EntityId stateB, const Color& color);
        /// Shouts a line across the top of the screen for a few seconds.
        void Announce(const std::string& headline, const std::string& detail, const Color& color);
        std::vector<Herald>& Heralds() { return m_heralds; }
        const std::vector<Herald>& Heralds() const { return m_heralds; }
        std::vector<DiplomaticFlare>& Flares() { return m_flares; }
        const std::vector<DiplomaticFlare>& Flares() const { return m_flares; }
        /// Throws a fist up over this spot for a few seconds.
        void MarkRevolt(const Vec2& position);
        std::vector<RevoltMark>& RevoltMarks() { return m_revoltMarks; }
        /// The seat of a realm: its leading house's greatest city, or any holding it has.
        const Settlement* CapitalOf(EntityId stateId) const;

        const std::vector<Chronicle>& ChronicleEntries() const { return m_chronicle; }

        // --- persistence ---------------------------------------------------------------------------------
        /// The whole world as a document. `includeLayers` carries the forest and field
        /// grids with it; a save wants them and a multiplayer snapshot usually does not,
        /// because they are by far the largest part of it and they change once a month.
        Json SaveObjects(bool includeLayers = true, bool includeCharacters = true) const;
        /// Replaces everything this world holds with what the document says. Used by a
        /// client receiving the host's snapshot: entities that have died on the host must
        /// go, and LoadObjects on its own only ever adds.
        /// `localPeer` is this machine's player: the realm that carries his id is the one
        /// this machine plays, whatever realm the host was playing when he wrote the snapshot.
        void AdoptObjects(const Json& node, const std::string& localPeer = std::string());
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
        std::vector<BanditCamp> m_banditCamps;
        std::vector<RoadSegment> m_roads;
        std::vector<DiplomaticFlare> m_flares;
        std::vector<RevoltMark> m_revoltMarks;
        std::vector<Herald> m_heralds;

        std::vector<Scope<IPlayer>> m_players;
        std::vector<Chronicle> m_chronicle;

        EntityId m_humanState = kInvalidId;
        EntityId m_nextId = 1;
        u32 m_seed = 1337;
    };
}
