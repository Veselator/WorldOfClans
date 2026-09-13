#include "World.h"
#include "../Systems/CoverageSystem.h"
#include "../Players/IPlayer.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <algorithm>

namespace woc
{
    void World::Reset()
    {
        // Fresh containers rather than cleared ones. A cleared hash map keeps the buckets
        // it grew to, and the order it walks its entries in depends on them - so a machine
        // that had shown a busier map on the title screen would walk its armies in a
        // different order from one that had not, and a lockstep party would part ways.
        m_characters = {};
        m_units = {};
        m_cohorts = {};
        m_settlements = {};
        m_clans = {};
        m_states = {};
        m_mines.clear();
        m_banditCamps.clear();
        m_roads.clear();
        m_players.clear();
        m_chronicle.clear();
        m_flares.clear();
        m_heralds.clear();
        m_revoltMarks.clear();
        m_humanState = kInvalidId;
        m_nextId = 1;

        ConfigManager& config = ConfigManager::Get();
        m_time.Configure(config.Int("simulation/startYear", 912),
                         config.Int("simulation/daysPerMonth", 30),
                         config.Int("simulation/monthsPerYear", 12));
    }

    bool World::LoadMap(const std::string& folder)
    {
        if (!MapLoader::Load(folder, m_map, m_mapInfo)) return false;
        if (m_mapInfo.seed != 0) m_seed = m_mapInfo.seed;
        return true;
    }

    // --- creation -------------------------------------------------------------------------

    Character& World::CreateCharacter()
    {
        const EntityId id = NextId();
        Character& character = m_characters[id];
        character.id = id;
        return character;
    }

    Unit& World::CreateUnit()
    {
        const EntityId id = NextId();
        Unit& unit = m_units[id];
        unit.id = id;
        return unit;
    }

    Cohort& World::CreateCohort()
    {
        const EntityId id = NextId();
        Cohort& cohort = m_cohorts[id];
        cohort.id = id;
        return cohort;
    }

    Settlement& World::CreateSettlement()
    {
        const EntityId id = NextId();
        Settlement& settlement = m_settlements[id];
        settlement.id = id;
        return settlement;
    }

    Clan& World::CreateClan()
    {
        const EntityId id = NextId();
        Clan& clan = m_clans[id];
        clan.id = id;
        return clan;
    }

    State& World::CreateState()
    {
        const EntityId id = NextId();
        State& state = m_states[id];
        state.id = id;
        return state;
    }

    MineSite& World::CreateMine()
    {
        MineSite mine;
        mine.id = NextId();
        m_mines.push_back(mine);
        return m_mines.back();
    }

    // --- lookup ----------------------------------------------------------------------------

    namespace
    {
        template <typename Map>
        auto* Lookup(Map& container, EntityId id)
        {
            const auto it = container.find(id);
            return it == container.end() ? nullptr : &it->second;
        }
    }

    Character* World::FindCharacter(EntityId id) { return Lookup(m_characters, id); }
    Unit* World::FindUnit(EntityId id) { return Lookup(m_units, id); }
    Cohort* World::FindCohort(EntityId id) { return Lookup(m_cohorts, id); }
    Settlement* World::FindSettlement(EntityId id) { return Lookup(m_settlements, id); }
    Clan* World::FindClan(EntityId id) { return Lookup(m_clans, id); }
    State* World::FindState(EntityId id) { return Lookup(m_states, id); }

    const Character* World::FindCharacter(EntityId id) const { return Lookup(m_characters, id); }
    const Unit* World::FindUnit(EntityId id) const { return Lookup(m_units, id); }
    const Cohort* World::FindCohort(EntityId id) const { return Lookup(m_cohorts, id); }
    const Settlement* World::FindSettlement(EntityId id) const { return Lookup(m_settlements, id); }
    const Clan* World::FindClan(EntityId id) const { return Lookup(m_clans, id); }
    const State* World::FindState(EntityId id) const { return Lookup(m_states, id); }

    MineSite* World::FindMine(EntityId id)
    {
        for (MineSite& mine : m_mines)
        {
            if (mine.id == id) return &mine;
        }
        return nullptr;
    }

    const MineSite* World::FindMine(EntityId id) const
    {
        return const_cast<World*>(this)->FindMine(id);
    }

    // --- destruction ------------------------------------------------------------------------

    void World::DestroyUnit(EntityId id)
    {
        Unit* unit = FindUnit(id);
        if (!unit) return;

        for (EntityId characterId : unit->characters) m_characters.erase(characterId);
        for (EntityId characterId : unit->wounded) m_characters.erase(characterId);
        if (Cohort* cohort = FindCohort(unit->cohort))
        {
            cohort->units.erase(std::remove(cohort->units.begin(), cohort->units.end(), id),
                                cohort->units.end());
        }
        m_units.erase(id);
    }

    void World::DestroyCohort(EntityId id)
    {
        Cohort* cohort = FindCohort(id);
        if (!cohort) return;

        const std::vector<EntityId> units = cohort->units;
        for (EntityId unitId : units)
        {
            if (Unit* unit = FindUnit(unitId))
            {
                for (EntityId characterId : unit->characters) m_characters.erase(characterId);
            }
            m_units.erase(unitId);
        }
        if (Clan* clan = FindClan(cohort->clan)) clan->RemoveCohort(id);
        m_cohorts.erase(id);
    }

    void World::DestroySettlement(EntityId id)
    {
        Settlement* settlement = FindSettlement(id);
        if (!settlement) return;
        if (Clan* clan = FindClan(settlement->owner)) clan->RemoveSettlement(id);

        // Anything besieging or garrisoning the settlement is released.
        for (auto& [cohortId, cohort] : m_cohorts)
        {
            if (cohort.garrisonOf == id) cohort.garrisonOf = kInvalidId;
            if (cohort.currentTask.targetSettlement == id) cohort.currentTask.Clear();
        }
        m_settlements.erase(id);
    }

    // --- relationships -------------------------------------------------------------------------

    State* World::StateOfClan(EntityId clanId)
    {
        Clan* clan = FindClan(clanId);
        return clan ? FindState(clan->state) : nullptr;
    }

    const State* World::StateOfClan(EntityId clanId) const
    {
        const Clan* clan = FindClan(clanId);
        return clan ? FindState(clan->state) : nullptr;
    }

    Clan* World::ClanOfSettlement(EntityId settlementId)
    {
        Settlement* settlement = FindSettlement(settlementId);
        return settlement ? FindClan(settlement->owner) : nullptr;
    }

    Clan* World::ClanOfCohort(EntityId cohortId)
    {
        Cohort* cohort = FindCohort(cohortId);
        return cohort ? FindClan(cohort->clan) : nullptr;
    }

    const Settlement* World::CapitalOf(EntityId stateId) const
    {
        const State* state = FindState(stateId);
        if (!state) return nullptr;

        const Settlement* best = nullptr;
        i32 bestPopulation = -1;
        for (EntityId clanId : state->clans)
        {
            const Clan* clan = FindClan(clanId);
            if (!clan) continue;
            for (EntityId settlementId : clan->settlements)
            {
                const Settlement* settlement = FindSettlement(settlementId);
                if (!settlement) continue;

                // A city outranks anything smaller whatever its head count.
                const i32 weight = settlement->population +
                    (settlement->kind == SettlementKind::City ? 100000 : 0) +
                    (settlement->kind == SettlementKind::Castle ? 50000 : 0);
                if (weight > bestPopulation) { bestPopulation = weight; best = settlement; }
            }
        }
        return best;
    }

    void World::Announce(const std::string& headline, const std::string& detail, const Color& color)
    {
        const f32 duration = ConfigManager::Get().Float("diplomacy/heraldSeconds", 5.0f);
        m_heralds.push_back({ headline, detail, color, duration, duration });
    }

    void World::MarkRevolt(const Vec2& position)
    {
        const f32 duration = ConfigManager::Get().Float("population/revoltMarkSeconds", 3.2f);
        m_revoltMarks.push_back({ position, duration, duration });
    }

    void World::Flare(EntityId stateA, EntityId stateB, const Color& color)
    {
        const Settlement* a = CapitalOf(stateA);
        const Settlement* b = CapitalOf(stateB);
        if (!a || !b || a->id == b->id) return;

        const f32 duration = ConfigManager::Get().Float("diplomacy/flareSeconds", 2.2f);
        m_flares.push_back({ a->position, b->position, color, duration, duration });
    }

    bool World::MayAttackSettlement(EntityId clanId, EntityId settlementId) const
    {
        const Settlement* settlement = FindSettlement(settlementId);
        if (!settlement) return false;
        if (settlement->owner == clanId) return false;

        // Robbers keep no borders and respect none. Anybody's village is theirs to plunder,
        // and whose country it stands in is not a question they ask.
        const State* ours = StateOfClan(clanId);
        if (ours && ours->outlaw) return true;

        if (settlement->owner == kInvalidId)
        {
            // An unclaimed or risen holding answers to nobody, but the country around it
            // answers to somebody. Riding into a neighbour's borders to take a village off
            // his hands is an act of war whatever the village thinks, so it is allowed only
            // where the ground is one's own or nobody's.
            const EntityId ground = CoverageSystem::Get().OwnerAt(*this, settlement->position);
            if (ground == kInvalidId || ground == clanId) return true;

            const State* ours = StateOfClan(clanId);
            const State* theirs = StateOfClan(ground);
            if (ours && theirs && ours->id == theirs->id) return true;

            // ...unless we are already at war with whoever holds the country, in which case
            // the village is one more thing to be taken from him.
            return AreHostile(clanId, ground);
        }
        return AreHostile(clanId, settlement->owner);
    }

    bool World::AreHostile(EntityId clanA, EntityId clanB) const
    {
        if (clanA == clanB) return false;
        if (clanA == kInvalidId || clanB == kInvalidId) return false;

        const State* stateA = StateOfClan(clanA);
        const State* stateB = StateOfClan(clanB);
        if (!stateA || !stateB) return false;
        if (stateA->id == stateB->id) return false;

        // Outlaws have no diplomacy to be at war through: they are simply everybody's
        // enemy, and each other's - two bands that meet in the same forest are competitors
        // long before they are colleagues.
        if (stateA->outlaw || stateB->outlaw) return true;

        return stateA->IsAtWarWith(stateB->id);
    }

    u32 World::CohortStrength(EntityId cohortId) const
    {
        const Cohort* cohort = FindCohort(cohortId);
        if (!cohort) return 0;

        u32 total = 0;
        for (EntityId unitId : cohort->units)
        {
            if (const Unit* unit = FindUnit(unitId)) total += unit->Strength();
        }
        return total;
    }

    f32 World::CohortPower(EntityId cohortId) const
    {
        const Cohort* cohort = FindCohort(cohortId);
        if (!cohort) return 0.0f;

        f32 total = 0.0f;
        for (EntityId unitId : cohort->units)
        {
            if (const Unit* unit = FindUnit(unitId)) total += unit->CombatPower(cohort->experience);
        }
        return total;
    }

    BanditCamp& World::CreateBanditCamp()
    {
        BanditCamp camp;
        camp.id = NextId();
        m_banditCamps.push_back(camp);
        return m_banditCamps.back();
    }

    BanditCamp* World::FindBanditCamp(EntityId id)
    {
        for (BanditCamp& camp : m_banditCamps)
        {
            if (camp.id == id) return &camp;
        }
        return nullptr;
    }

    const BanditCamp* World::FindBanditCamp(EntityId id) const
    {
        for (const BanditCamp& camp : m_banditCamps)
        {
            if (camp.id == id) return &camp;
        }
        return nullptr;
    }

    void World::DestroyBanditCamp(EntityId id)
    {
        m_banditCamps.erase(std::remove_if(m_banditCamps.begin(), m_banditCamps.end(),
                                           [id](const BanditCamp& camp) { return camp.id == id; }),
                            m_banditCamps.end());
    }

    Settlement* World::NearestSettlement(const Vec2& position, f32 maxDistance, EntityId ownerFilter)
    {
        Settlement* best = nullptr;
        f32 bestDistance = maxDistance * maxDistance;
        for (auto& [id, settlement] : m_settlements)
        {
            if (ownerFilter != kInvalidId && settlement.owner != ownerFilter) continue;
            const f32 distance = DistanceSq(settlement.position, position);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &settlement;
            }
        }
        return best;
    }

    std::vector<EntityId> World::SettlementsWithin(const Vec2& position, f32 radius) const
    {
        std::vector<EntityId> result;
        const f32 radiusSq = radius * radius;
        for (const auto& [id, settlement] : m_settlements)
        {
            if (DistanceSq(settlement.position, position) <= radiusSq) result.push_back(id);
        }
        return result;
    }

    std::vector<EntityId> World::CohortsWithin(const Vec2& position, f32 radius) const
    {
        std::vector<EntityId> result;
        const f32 radiusSq = radius * radius;
        for (const auto& [id, cohort] : m_cohorts)
        {
            if (DistanceSq(cohort.position, position) <= radiusSq) result.push_back(id);
        }
        return result;
    }

    // --- players ------------------------------------------------------------------------------------

    void World::AddPlayer(Scope<IPlayer> player)
    {
        m_players.push_back(std::move(player));
    }

    IPlayer* World::PlayerForState(EntityId stateId)
    {
        for (const Scope<IPlayer>& player : m_players)
        {
            if (player->StateId() == stateId) return player.get();
        }
        return nullptr;
    }

    Clan* World::HumanClan()
    {
        State* state = HumanState();
        return state ? FindClan(state->leader) : nullptr;
    }

    // --- chronicle ------------------------------------------------------------------------------------

    void World::Log(const std::string& text, const Color& color)
    {
        m_chronicle.push_back({ m_time.TotalDays(), text, color });
        if (m_chronicle.size() > 400) m_chronicle.erase(m_chronicle.begin());
        WOC_LOG_TRACE("[chronicle] ", text);
    }

    // --- persistence -------------------------------------------------------------------------------------

    void World::AdoptObjects(const Json& node, const std::string& localPeer)
    {
        // Everything that lives in the world goes; the map, the players and the chronicle
        // stay, because they are this machine's and not the host's.
        const EntityId ownRealm = m_humanState;

        // The people in the ranks are most of a snapshot's weight and change slowly, so
        // the host sends them only now and then; between those the ones we have stand.
        const bool withPeople = node.Has("characters");
        // Fresh containers, for the same reason as in Reset: equal worlds must also be laid
        // out equally in memory, or they are walked in different orders.
        if (withPeople) m_characters = {};
        m_units = {};
        m_cohorts = {};
        m_settlements = {};
        m_clans = {};
        m_states = {};
        m_mines.clear();
        m_roads.clear();
        // Camps were never cleared here, so every snapshot added the whole list again and a
        // guest's world filled up with thousands of copies of the same tents.
        m_banditCamps.clear();

        LoadObjects(node, true);

        // The host's calendar. The guest's own clock runs between snapshots for the sake
        // of a smooth picture and is pulled back into line here whenever it has drifted.
        const i32 hostDay = node["day"].AsInt(m_time.TotalDays());
        if (hostDay != m_time.TotalDays()) m_time.SetTotalDays(hostDay);

        // The snapshot names the host's realm as "the human one". This machine plays its
        // own: the realm whose seat carries this player's id.
        m_humanState = ownRealm;
        if (!localPeer.empty())
        {
            for (const auto& [id, state] : m_states)
            {
                if (state.peerId == localPeer) { m_humanState = id; break; }
            }
        }
    }

    namespace
    {
        /// Entities are written in id order, so the same world always gives the same text -
        /// whatever order its containers happen to hold them in.
        template <typename Map>
        std::vector<EntityId> SortedIds(const Map& map)
        {
            std::vector<EntityId> ids;
            ids.reserve(map.size());
            for (const auto& entry : map) ids.push_back(entry.first);
            std::sort(ids.begin(), ids.end());
            return ids;
        }
    }

    Json World::SaveObjects(bool includeLayers, bool includeCharacters) const
    {
        Json root = Json::MakeObject();
        root["seed"] = static_cast<i64>(m_seed);
        root["day"] = m_time.TotalDays();
        root["humanState"] = EncodeId(m_humanState);
        // The next id to hand out. Not "the highest id plus one": ids of the fallen are never
        // reused, so after a war the counter stands well above anybody still alive, and a
        // world that forgot that would name its next recruits differently.
        root["nextId"] = static_cast<i64>(m_nextId);

        Json states = Json::MakeArray();
        for (EntityId id : SortedIds(m_states)) states.Push(m_states.at(id).ToJson());
        root["states"] = states;

        Json clans = Json::MakeArray();
        for (EntityId id : SortedIds(m_clans)) clans.Push(m_clans.at(id).ToJson());
        root["clans"] = clans;

        Json settlements = Json::MakeArray();
        for (EntityId id : SortedIds(m_settlements)) settlements.Push(m_settlements.at(id).ToJson());
        root["settlements"] = settlements;

        Json cohorts = Json::MakeArray();
        for (EntityId id : SortedIds(m_cohorts)) cohorts.Push(m_cohorts.at(id).ToJson());
        root["cohorts"] = cohorts;

        Json units = Json::MakeArray();
        for (EntityId id : SortedIds(m_units)) units.Push(m_units.at(id).ToJson());
        root["units"] = units;

        if (includeCharacters)
        {
            Json characters = Json::MakeArray();
            for (EntityId id : SortedIds(m_characters)) characters.Push(m_characters.at(id).ToJson());
            root["characters"] = characters;
        }

        Json mines = Json::MakeArray();
        for (const MineSite& mine : m_mines)
        {
            Json node = Json::MakeObject();
            node["id"] = EncodeId(mine.id);
            node["x"] = mine.position.x;
            node["y"] = mine.position.y;
            node["resource"] = ResourceData::Name(mine.resource);
            node["richness"] = mine.richness;
            node["owner"] = EncodeId(mine.owner);
            node["developed"] = mine.developed;
            node["daysLeft"] = mine.daysRemaining;
            node["daysTotal"] = mine.daysTotal;
            mines.Push(node);
        }
        root["mines"] = mines;

        Json camps = Json::MakeArray();
        for (const BanditCamp& camp : m_banditCamps)
        {
            Json node = Json::MakeObject();
            node["id"] = EncodeId(camp.id);
            node["clan"] = EncodeId(camp.clan);
            node["x"] = camp.position.x;
            node["y"] = camp.position.y;
            node["race"] = camp.raceId;
            node["hoard"] = camp.hoard.ToJson();
            node["strength"] = camp.strength;
            node["damage"] = camp.damage;
            node["muster"] = camp.musterDays;
            camps.Push(node);
        }
        root["banditCamps"] = camps;

        Json roads = Json::MakeArray();
        for (const RoadSegment& road : m_roads)
        {
            Json node = Json::MakeObject();
            node["level"] = road.level;
            Json points = Json::MakeArray();
            for (const Vec2& point : road.points)
            {
                Json pair = Json::MakeArray();
                pair.Push(point.x);
                pair.Push(point.y);
                points.Push(pair);
            }
            node["points"] = points;
            roads.Push(node);
        }
        root["roads"] = roads;

        // Forest and field coverage are grid layers, stored run-length encoded to keep
        // MapObjects.json readable rather than exploding into a million numbers.
        if (!includeLayers) return root;

        auto encodeLayer = [this](bool forest)
        {
            Json runs = Json::MakeArray();
            const std::vector<Tile>& tiles = m_map.Tiles();
            u8 current = 0;
            i32 length = 0;
            for (const Tile& tile : tiles)
            {
                const u8 value = static_cast<u8>(Clamp01(forest ? tile.forest : tile.field) * 255.0f + 0.5f);
                if (value == current) { ++length; continue; }
                if (length > 0)
                {
                    Json run = Json::MakeArray();
                    run.Push(static_cast<i64>(current));
                    run.Push(length);
                    runs.Push(run);
                }
                current = value;
                length = 1;
            }
            if (length > 0)
            {
                Json run = Json::MakeArray();
                run.Push(static_cast<i64>(current));
                run.Push(length);
                runs.Push(run);
            }
            return runs;
        };
        root["forestRuns"] = encodeLayer(true);
        root["fieldRuns"] = encodeLayer(false);
        return root;
    }

    void World::LoadObjects(const Json& node, bool includeRealms)
    {
        m_seed = static_cast<u32>(node["seed"].AsInt(1337));

        EntityId highest = 0;
        auto track = [&highest](EntityId id) { if (id != kInvalidId && id > highest) highest = id; };

        // Realms are optional: a new party keeps the authored landscape but seeds its own
        // states, houses and settlements from the party settings.
        if (includeRealms)
        {
            m_humanState = DecodeId(node["humanState"]);

            for (const Json& entry : node["states"].AsArray())
            {
                State state = State::FromJson(entry);
                track(state.id);
                m_states[state.id] = std::move(state);
            }
            for (const Json& entry : node["clans"].AsArray())
            {
                Clan clan = Clan::FromJson(entry);
                track(clan.id);
                m_clans[clan.id] = std::move(clan);
            }
            for (const Json& entry : node["settlements"].AsArray())
            {
                Settlement settlement = Settlement::FromJson(entry);
                track(settlement.id);
                m_settlements[settlement.id] = std::move(settlement);
            }
            for (const Json& entry : node["cohorts"].AsArray())
            {
                Cohort cohort = Cohort::FromJson(entry);
                track(cohort.id);
                m_cohorts[cohort.id] = std::move(cohort);
            }
            for (const Json& entry : node["units"].AsArray())
            {
                Unit unit = Unit::FromJson(entry);
                track(unit.id);
                m_units[unit.id] = std::move(unit);
            }
            for (const Json& entry : node["characters"].AsArray())
            {
                Character character = Character::FromJson(entry);
                track(character.id);
                m_characters[character.id] = std::move(character);
            }
        }

        for (const Json& entry : node["mines"].AsArray())
        {
            MineSite mine;
            mine.id = DecodeId(entry["id"]);
            mine.position = { entry["x"].AsFloat(), entry["y"].AsFloat() };
            mine.resource = ResourceData::Parse(entry["resource"].AsString("stone"));
            mine.richness = entry["richness"].AsFloat(1.0f);
            mine.owner = DecodeId(entry["owner"]);
            mine.developed = entry["developed"].AsBool(false);
            mine.daysRemaining = entry["daysLeft"].AsFloat(0.0f);
            mine.daysTotal = entry["daysTotal"].AsFloat(0.0f);
            track(mine.id);
            m_mines.push_back(mine);
        }
        for (const Json& entry : node["banditCamps"].AsArray())
        {
            BanditCamp camp;
            camp.id = DecodeId(entry["id"]);
            camp.clan = DecodeId(entry["clan"]);
            camp.position = { entry["x"].AsFloat(), entry["y"].AsFloat() };
            camp.raceId = entry["race"].AsString("human");
            camp.hoard = ResourceData::FromJson(entry["hoard"]);
            camp.strength = entry["strength"].AsFloat(1.0f);
            camp.damage = entry["damage"].AsFloat(0.0f);
            camp.musterDays = entry["muster"].AsFloat(0.0f);
            track(camp.id);
            m_banditCamps.push_back(camp);
        }
        for (const Json& entry : node["roads"].AsArray())
        {
            RoadSegment road;
            road.level = entry["level"].AsInt(1);
            for (const Json& point : entry["points"].AsArray())
            {
                road.points.push_back({ point[static_cast<size_t>(0)].AsFloat(),
                                        point[static_cast<size_t>(1)].AsFloat() });
            }
            m_roads.push_back(std::move(road));
        }

        auto decodeLayer = [this](const Json& runs, bool forest)
        {
            if (runs.Size() == 0) return;
            std::vector<Tile>& tiles = m_map.Tiles();
            size_t cursor = 0;
            for (const Json& run : runs.AsArray())
            {
                const f32 value = static_cast<f32>(run[static_cast<size_t>(0)].AsInt(0)) / 255.0f;
                const i32 length = run[static_cast<size_t>(1)].AsInt(0);
                for (i32 i = 0; i < length && cursor < tiles.size(); ++i, ++cursor)
                {
                    if (forest) tiles[cursor].forest = value;
                    else tiles[cursor].field = value;
                }
            }
        };
        decodeLayer(node["forestRuns"], true);
        decodeLayer(node["fieldRuns"], false);

        for (const auto& [id, character] : m_characters) track(id);
        m_nextId = std::max<EntityId>(highest + 1, static_cast<EntityId>(node["nextId"].AsNumber(0.0)));
    }
}
