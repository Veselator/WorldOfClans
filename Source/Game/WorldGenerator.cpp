#include "WorldGenerator.h"

#include "Factories/NamePool.h"
#include "Factories/SettlementFactory.h"
#include "Factories/UnitFactory.h"
#include "Map/MapLoader.h"
#include "Players/AIPlayer.h"
#include "Players/HumanPlayer.h"
#include "Systems/BanditSystem.h"
#include "Systems/CoverageSystem.h"
#include "Systems/RoadSystem.h"
#include "Systems/DynastySystem.h"
#include "Systems/FogSystem.h"
#include "Systems/MarketSystem.h"
#include "Systems/ForestrySystem.h"
#include "Systems/Simulation.h"
#include "World/RaceDatabase.h"
#include "World/World.h"
#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"
#include "../Core/Random.h"
#include "../Render/Renderer.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace woc
{
    namespace
    {
        constexpr const char* kObjectsFile = "MapObjects.json";

        Color ClanColorForIndex(size_t index)
        {
            const Json& colors = ConfigManager::Get().Game()["clanColors"];
            if (colors.Size() == 0) return Color::FromRGB(0xC8452D);
            const std::string hex = colors[index % colors.Size()].AsString("c8452d");
            return Color::FromRGB(static_cast<u32>(std::strtoul(hex.c_str(), nullptr, 16)));
        }

        /// Scores how good a spot is for a capital: fertile, defensible, not on the shore edge.
        f32 SiteQuality(const MapData& map, const Vec2& position)
        {
            const TerrainInfo& terrain = map.TerrainAtMap(position);
            if (!terrain.buildable || terrain.water) return -1.0f;

            const f32 soil = map.SampleSoil(position, 140.0f);
            const f32 water = map.SampleWater(position, 140.0f);
            const f32 forest = map.SampleForest(position, 140.0f);
            const f32 stone = map.SampleStone(position, 180.0f);

            // Water nearby is good (rivers, coast) but drowning in it is not.
            const f32 waterScore = water < 0.35f ? water * 2.0f : (0.7f - water);
            return soil * 2.0f + waterScore + forest * 0.8f + stone * 0.5f + terrain.defense * 0.4f;
        }
    }

    Json SeatSettings::ToJson() const
    {
        Json node = Json::MakeObject();
        node["race"] = raceId;                  // empty = drawn at random
        node["color"] = static_cast<i64>(color);
        node["human"] = human;
        if (!playerName.empty()) node["name"] = playerName;
        if (!peerId.empty()) node["peer"] = peerId;
        return node;
    }

    SeatSettings SeatSettings::FromJson(const Json& node)
    {
        SeatSettings seat;
        seat.raceId = node["race"].AsString();
        seat.color = static_cast<u32>(node["color"].AsNumber(0xC8452D));
        seat.human = node["human"].AsBool(false);
        seat.playerName = node["name"].AsString();
        seat.peerId = node["peer"].AsString();
        return seat;
    }

    SeatSettings PartySettings::SeatFor(size_t index) const
    {
        if (index < seats.size()) return seats[index];

        // A party with more realms than seats fills the rest in with the machine: this is
        // what "додати ще державу" does before anybody has said anything about it.
        SeatSettings seat;
        seat.human = false;
        return seat;
    }

    Json PartySettings::ToJson() const
    {
        Json node = Json::MakeObject();
        node["map"] = mapFolder;
        node["seed"] = static_cast<i64>(seed);
        node["states"] = stateCount;
        node["aiAggression"] = aiAggression;
        node["fogOfWar"] = fogOfWar;
        node["bandits"] = bandits.ToJson();
        node["humanSeat"] = humanSeat;
        node["generateMap"] = generateMap;
        node["mapGen"] = mapGen.ToJson();

        Json list = Json::MakeArray();
        for (const SeatSettings& seat : seats) list.Push(seat.ToJson());
        node["seats"] = list;
        return node;
    }

    PartySettings PartySettings::FromJson(const Json& node)
    {
        PartySettings settings;
        settings.mapFolder = node["map"].AsString("Test");
        settings.seed = static_cast<u32>(node["seed"].AsNumber(0.0));
        settings.stateCount = node["states"].AsInt(5);
        settings.aiAggression = node["aiAggression"].AsFloat(0.55f);
        settings.fogOfWar = node["fogOfWar"].AsBool(false);
        settings.bandits = BanditSettings::FromJson(node["bandits"]);
        settings.humanSeat = node["humanSeat"].AsInt(0);
        settings.generateMap = node["generateMap"].AsBool(false);
        settings.mapGen = MapGenSettings::FromJson(node["mapGen"]);

        for (const Json& seat : node["seats"].AsArray())
        {
            settings.seats.push_back(SeatSettings::FromJson(seat));
        }
        return settings;
    }

    bool WorldGenerator::LoadObjects(World& world, const std::string& mapFolder, bool includeRealms)
    {
        const std::string path = Paths::Get().Map(mapFolder, kObjectsFile);
        if (!Paths::FileExists(path)) return false;

        std::string error;
        const Json doc = Json::LoadFile(path, &error);
        if (!error.empty() || doc.IsNull())
        {
            WOC_LOG_WARN("MapObjects.json for '", mapFolder, "' could not be read: ", error);
            return false;
        }

        world.LoadObjects(doc, includeRealms);
        WOC_LOG_INFO("Loaded ", kObjectsFile, ": ", world.Settlements().size(), " settlements, ",
                     world.Mines().size(), " mines, ", world.Roads().size(), " roads");
        return includeRealms ? !world.Settlements().empty() : true;
    }

    bool WorldGenerator::SaveObjects(const World& world, const std::string& mapFolder)
    {
        const std::string path = Paths::Get().Map(mapFolder, kObjectsFile);
        return world.SaveObjects().SaveFile(path);
    }

    void WorldGenerator::PublishMapToRenderer(const MapData& map)
    {
        Renderer& renderer = Renderer::Get();

        renderer.SetTerrainColor(map.ColorPixels(), map.PixelWidth(), map.PixelHeight());
        renderer.SetTreeMask(map.BuildForestMask(), map.TileWidth(), map.TileHeight());
        renderer.SetFieldMask(map.BuildFieldMask(), map.TileWidth(), map.TileHeight());
        renderer.SetOwnerMask(map.BuildOwnerMask(), map.TileWidth(), map.TileHeight());
        renderer.SetRoadMask(std::vector<u8>(1, 0), 1, 1);
        renderer.SetShoreMask(map.BuildShoreMask(ConfigManager::Get().Int("render/water/foamReach", 3)),
                               map.TileWidth(), map.TileHeight());


        const u32 step = static_cast<u32>(ConfigManager::Get().Int("render/terrainMeshStep", 8));
        renderer.SetTerrainMesh(MapLoader::BuildMesh(map, step));
        renderer.SetWaterPlane({ static_cast<f32>(map.PixelWidth()), static_cast<f32>(map.PixelHeight()) },
                               ConfigManager::Get().Float("render/water/planeMargin", 8000.0f));

        Camera& camera = renderer.GetCamera();
        camera.SetBounds({ 0.0f, 0.0f },
                         { static_cast<f32>(map.PixelWidth()), static_cast<f32>(map.PixelHeight()) });
        camera.SetFocus({ map.PixelWidth() * 0.5f, map.PixelHeight() * 0.5f });
    }

    void WorldGenerator::PublishMapToRenderer(World& world)
    {
        PublishMapToRenderer(world.Map());
    }

    bool WorldGenerator::Generate(World& world, const PartySettings& settings)
    {
        world.Reset();
        NamePool::Get().Load();
        NamePool::Get().ResetUsed();

        // A generated world is written into Maps/ before anything else happens, so that from
        // this point on it is an ordinary map: the save will name a folder that exists, the
        // editor can open it, and another player can be sent it.
        std::string mapFolder = settings.mapFolder;
        if (settings.generateMap)
        {
            MapDescription made;
            mapFolder = MapGenerator::GenerateAndStore(settings.mapGen, made);
            if (mapFolder.empty())
            {
                WOC_LOG_ERROR("Could not generate a map for this party");
                return false;
            }
            WOC_LOG_INFO("Party starts on a generated map: ", mapFolder);
        }

        if (!world.LoadMap(mapFolder))
        {
            WOC_LOG_ERROR("Could not load map '", mapFolder, "'");
            return false;
        }

        const u32 seed = settings.seed != 0 ? settings.seed : static_cast<u32>(std::time(nullptr));
        world.SetSeed(seed);
        GlobalRandom().Seed(seed);

        // A new party always gets fresh realms; the map file only supplies its furniture
        // (roads, mines and the forest and field layers the editor painted).
        const bool hasFurniture = LoadObjects(world, mapFolder, false);
        SeedRealms(world, settings);
        if (world.Mines().empty()) SeedMines(world);
        if (!hasFurniture) WOC_LOG_TRACE("No MapObjects.json for this map; using bare terrain");

        // The robbers go down after the realms, so that their camps can be kept well away
        // from everybody's villages.
        BanditSystem::Get().Reset();
        BanditSystem::Get().Seed(world, settings.bandits, GlobalRandom());

        CreatePlayers(world, settings);
        RoadSystem::Get().Reset();
        RoadSystem::Get().StampExisting(world);
        ForestrySystem::Get().ResetPlantations();
        ForestrySystem::Get().ClearUnarableFields(world);
        ForestrySystem::Get().ClearUnforestable(world);

        // Towns that came with the map are turned this way or that afresh every party, so
        // the same map never looks quite the same twice.
        for (auto& [id, settlement] : world.Settlements()) settlement.mirrored = GlobalRandom().Chance(0.5f);

        FogSystem::Get().SetEnabled(settings.fogOfWar);
        FogSystem::Get().Reset(world);
        MarketSystem::Get().Reset();
        PublishMapToRenderer(world);

        Simulation::Get().Reset();
        CoverageSystem::Get().MarkDirty();
        CoverageSystem::Get().RecomputeBlocking(world);

        // The first border pass sweeps the free villages standing inside a capital's reach
        // into that realm. Whatever a realm holds on the first morning, it holds as its own
        // people: a prince does not begin his reign with a ring of foreign, foreign-worshipping
        // hamlets already halfway to rebellion. Anything conquered later keeps its own, which
        // is where the interesting trouble is supposed to come from.
        i32 strays = 0;
        for (auto& [id, settlement] : world.Settlements())
        {
            const Clan* owner = world.FindClan(settlement.owner);
            if (!owner) continue;
            if (CoverageSystem::Get().OwnerAt(world, settlement.position) != owner->id) ++strays;
            settlement.raceId = owner->raceId;
            settlement.faithId = owner->faithId;
        }

        if (strays > 0) WOC_LOG_WARN("Generation left ", strays, " settlements outside their owner's borders");

        world.Log("Починається " + std::to_string(world.Time().Year()) + " рік від Різдва Христового",
                  Color::FromRGB(0xC9A227));
        WOC_LOG_INFO("World generated: ", world.States().size(), " states, ",
                     world.Clans().size(), " clans, ", world.Settlements().size(), " settlements");
        return true;
    }

    void WorldGenerator::SeedRealms(World& world, const PartySettings& settings)
    {
        const MapData& map = world.Map();
        Random& random = GlobalRandom();
        NamePool& names = NamePool::Get();
        const RaceDatabase& races = RaceDatabase::Get();

        // --- find well-spread, high-quality capital sites --------------------------------------
        struct Candidate { Vec2 position; f32 quality; };
        std::vector<Candidate> candidates;

        const i32 samples = 4000;
        for (i32 i = 0; i < samples; ++i)
        {
            const Vec2 position{
                random.RangeF(60.0f, static_cast<f32>(map.PixelWidth()) - 60.0f),
                random.RangeF(60.0f, static_cast<f32>(map.PixelHeight()) - 60.0f)
            };
            const f32 quality = SiteQuality(map, position);
            if (quality > 0.0f) candidates.push_back({ position, quality });
        }
        std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.quality > b.quality; });

        const f32 minimumSeparation = 300.0f;
        std::vector<Vec2> capitals;
        for (const Candidate& candidate : candidates)
        {
            if (static_cast<i32>(capitals.size()) >= settings.stateCount) break;
            const bool tooClose = std::any_of(capitals.begin(), capitals.end(),
                [&](const Vec2& other) { return Distance(other, candidate.position) < minimumSeparation; });
            if (tooClose) continue;
            capitals.push_back(candidate.position);
        }

        if (capitals.empty())
        {
            WOC_LOG_ERROR("No viable capital sites found on this map");
            return;
        }

        // --- build a realm around each capital ----------------------------------------------------
        const std::vector<RaceInfo>& raceList = races.Races();

        // Where each realm stands on the map is a draw; which realm is whose is not. The
        // seats are shuffled onto the capitals so the player's own place is different every
        // party, while his banner, his people and his neighbours are exactly what was set.
        // The villages go down last, once every capital and castle stands and the borders
        // have been drawn from them: a realm's village must lie on that realm's own ground.
        struct Realm { EntityId clan; Vec2 seat; std::string raceId; std::string faithId; };
        std::vector<Realm> realms;

        std::vector<size_t> seating(capitals.size());
        for (size_t i = 0; i < seating.size(); ++i) seating[i] = i;
        random.Shuffle(seating);

        for (size_t index = 0; index < capitals.size(); ++index)
        {
            const size_t seatIndex = seating[index];
            const SeatSettings seat = settings.SeatFor(seatIndex);
            // The seat this machine plays, and every seat a person plays anywhere: those are
            // steered by orders, never by an AI - on every machine alike, or the machines
            // would each run a different party.
            const bool isPlayer = static_cast<i32>(seatIndex) == settings.humanSeat || seat.human;
            const bool isLocal = static_cast<i32>(seatIndex) == settings.humanSeat;

            // An empty race on a seat means "draw one" - that is what the party screen's
            // "випадковий" choice writes, and what an unset seat inherits.
            std::string raceId = seat.raceId;
            const bool known = std::any_of(raceList.begin(), raceList.end(),
                [&raceId](const RaceInfo& race) { return race.id == raceId; });
            if (!known)
            {
                raceId = raceList.empty()
                    ? std::string("human")
                    : raceList[static_cast<size_t>(random.Range(0, static_cast<i32>(raceList.size()) - 1))].id;
            }
            const RaceInfo& race = races.Race(raceId);
            const Color banner = Color::FromRGB(seat.color);

            State& state = world.CreateState();
            state.raceId = raceId;
            state.name = seat.playerName.empty() ? names.StateName(raceId, random) : seat.playerName;
            state.color = banner;
            state.playerControlled = isPlayer;
            // In a multiplayer party the seat names the player who holds it, and that is
            // what the host checks every incoming order against.
            state.peerId = seat.peerId;
            if (isLocal) world.SetHumanState(state.id);

            Clan& clan = world.CreateClan();
            clan.state = state.id;
            clan.raceId = raceId;
            clan.faithId = race.defaultFaith;
            clan.name = names.ClanName(raceId, random);
            clan.color = state.color;
            clan.resources = { 600.0f, 240.0f, 180.0f, 400.0f };
            state.AddClan(clan.id);

            // Capital city.
            SettlementRequest capital;
            capital.kind = SettlementKind::City;
            capital.position = capitals[index];
            capital.raceId = raceId;
            capital.faithId = race.defaultFaith;
            capital.owner = clan.id;
            capital.population = random.Range(3200, 5200);
            Settlement& city = SettlementFactory::Create(world, capital, random);
            city.buildings.push_back("palisade");

            DynastySystem::Get().FoundDynasty(world, clan.id, city.name);

            // A castle to hold the approaches.
            Vec2 castleSite;
            // Close enough that the capital's coverage already reaches it.
            if (SettlementFactory::FindSite(world, map, SettlementKind::Castle, city.position,
                                            190.0f, random, castleSite))
            {
                SettlementRequest castle;
                castle.kind = SettlementKind::Castle;
                castle.position = castleSite;
                castle.raceId = raceId;
                castle.faithId = race.defaultFaith;
                castle.owner = clan.id;
                // No fence on a castle: a castle is the wall.
                SettlementFactory::Create(world, castle, random);
            }

            realms.push_back({ clan.id, city.position, raceId, race.defaultFaith });

            // The prince's retinue musters outside the walls, not inside them.
            Cohort& retinue = UnitFactory::CreateRetinue(world, clan.id, city,
                                                         static_cast<u32>(random.Range(4, 6)), random);
            retinue.garrisonOf = kInvalidId;
            retinue.currentTask.Clear();

            const f32 angle = random.RangeF(0.0f, 2.0f * kPi);
            const f32 muster = ConfigManager::Get().Float("render/spriteScale/settlement", 26.0f) * 1.8f;
            Vec2 camp{ city.position.x + std::cos(angle) * muster,
                       city.position.y + std::sin(angle) * muster };
            if (!map.IsPassable(map.ToTile(camp))) camp = city.position;
            retinue.position = camp;

            WOC_LOG_INFO("Realm ", index, ": ", state.name, " (", raceId, "), seat ", city.name,
                         isPlayer ? " [player]" : "");
        }

        // --- the borders as the capitals and castles draw them -------------------------------------
        CoverageSystem& coverage = CoverageSystem::Get();
        coverage.RecomputeBlocking(world);

        // --- two or three villages for each realm, inside its own borders --------------------------
        for (const Realm& realm : realms)
        {
            const int villageCount = random.Range(2, 3);
            for (int v = 0; v < villageCount; ++v)
            {
                Vec2 site;
                // Passing the clan makes the factory insist the spot is under that clan's sway.
                if (!SettlementFactory::FindSite(world, map, SettlementKind::Village, realm.seat,
                                                 170.0f, random, site, realm.clan))
                {
                    continue;
                }
                SettlementRequest village;
                village.kind = SettlementKind::Village;
                village.position = site;
                village.raceId = realm.raceId;
                village.faithId = realm.faithId;
                village.owner = realm.clan;
                SettlementFactory::Create(world, village, random);
            }
        }

        // --- a scattering of independent villages, the free countryside ----------------------------
        // Only on ground nobody claims yet; a "free" village inside a realm would simply be
        // swept into it on the first border pass.
        const i32 freeVillages = std::max(4, settings.stateCount * 2);
        for (i32 i = 0; i < freeVillages; ++i)
        {
            Vec2 site;
            bool found = false;
            for (i32 attempt = 0; attempt < 8 && !found; ++attempt)
            {
                const Vec2 origin{
                    random.RangeF(60.0f, static_cast<f32>(map.PixelWidth()) - 60.0f),
                    random.RangeF(60.0f, static_cast<f32>(map.PixelHeight()) - 60.0f)
                };
                found = SettlementFactory::FindSite(world, map, SettlementKind::Village, origin, 220.0f, random, site) &&
                        coverage.OwnerAt(world, site) == kInvalidId;
            }
            if (!found) continue;

            const std::string raceId = raceList.empty() ? "human"
                : raceList[static_cast<size_t>(random.Range(0, static_cast<i32>(raceList.size()) - 1))].id;

            SettlementRequest village;
            village.kind = SettlementKind::Village;
            village.position = site;
            village.raceId = raceId;
            village.faithId = races.Race(raceId).defaultFaith;
            village.owner = kInvalidId;
            SettlementFactory::Create(world, village, random);
        }
    }

    void WorldGenerator::SeedMines(World& world)
    {
        const MapData& map = world.Map();
        Random& random = GlobalRandom();

        // Quarries belong where the rock is: hills and highlands.
        const i32 attempts = 900;
        i32 placed = 0;
        for (i32 i = 0; i < attempts && placed < 18; ++i)
        {
            const Vec2 position{
                random.RangeF(30.0f, static_cast<f32>(map.PixelWidth()) - 30.0f),
                random.RangeF(30.0f, static_cast<f32>(map.PixelHeight()) - 30.0f)
            };
            const TerrainInfo& terrain = map.TerrainAtMap(position);
            if (!terrain.mineable || !terrain.passable) continue;

            const bool tooClose = std::any_of(world.Mines().begin(), world.Mines().end(),
                [&](const MineSite& other) { return Distance(other.position, position) < 120.0f; });
            if (tooClose) continue;

            MineSite& mine = world.CreateMine();
            mine.position = position;
            mine.resource = ResourceType::Stone;
            mine.richness = terrain.stone * random.RangeF(0.8f, 1.3f);
            ++placed;
        }
    }

    void WorldGenerator::CreatePlayers(World& world, const PartySettings& settings)
    {
        std::vector<EntityId> stateIds;
        for (const auto& [id, state] : world.States()) stateIds.push_back(id);
        std::sort(stateIds.begin(), stateIds.end());
        if (stateIds.empty()) return;

        // SeedRealms already flagged the drawn seat; fall back to the first realm when a
        // whole world was loaded from file and nobody was marked.
        bool anyPlayer = false;
        for (EntityId id : stateIds)
        {
            const State* state = world.FindState(id);
            if (state && state->playerControlled) { anyPlayer = true; break; }
        }
        if (!anyPlayer && !stateIds.empty())
        {
            if (State* first = world.FindState(stateIds.front())) first->playerControlled = true;
        }

        for (size_t i = 0; i < stateIds.size(); ++i)
        {
            State* state = world.FindState(stateIds[i]);
            if (!state) continue;
            // The outlaws are driven by BanditSystem and by nothing else: an AIPlayer would
            // try to build them a granary.
            if (state->outlaw) continue;

            if (state->playerControlled)
            {
                if (world.HumanStateId() == kInvalidId) world.SetHumanState(state->id);
                world.AddPlayer(MakeScope<HumanPlayer>(state->id));
                WOC_LOG_INFO("Player seat: ", state->name, " (", state->raceId, ")");
            }
            else
            {
                state->playerControlled = false;
                world.AddPlayer(MakeScope<AIPlayer>(state->id, world.Seed() + static_cast<u32>(i) * 7919u));
            }
        }
    }
}
