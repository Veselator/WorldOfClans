#include "SaveGame.h"

#include "Players/AIPlayer.h"
#include "Players/HumanPlayer.h"
#include "Systems/CoverageSystem.h"
#include "Systems/FogSystem.h"
#include "Systems/MarketSystem.h"
#include "Systems/BanditSystem.h"
#include "Systems/PopulationSystem.h"
#include "Systems/BattleSystem.h"
#include "Systems/DiplomacySystem.h"
#include "../Core/Random.h"
#include "Systems/ForestrySystem.h"
#include "Systems/RoadSystem.h"
#include "Systems/Simulation.h"
#include "World/World.h"
#include "../Core/Json.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"

#include <algorithm>
#include <cstring>

namespace woc
{
    namespace
    {
        constexpr const char* kExtension = ".wocsave";
        constexpr i32 kFormatVersion = 1;

        std::string SlotPath(const std::string& fileName)
        {
            return Paths::Get().SavesDirectory() + "/" + fileName;
        }
    }

    std::string SaveGame::SanitiseName(const std::string& name)
    {
        std::string clean;
        clean.reserve(name.size());
        for (char c : name)
        {
            // Keep it simple and portable: nothing a file system could object to.
            const bool bad = c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
                             c == '"' || c == '<' || c == '>' || c == '|';
            clean += bad ? '_' : c;
        }
        while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
        return clean.empty() ? "save" : clean;
    }

    namespace
    {
        void WriteProcesses(const World& world, Json& root);
        void ReadProcesses(World& world, const Json& root);
        void SeatPlayers(World& world, bool multiplayer);
    }

    bool SaveGame::Save(const World& world, const std::string& slotName, const std::string& mapFolder)
    {
        if (!Paths::EnsureDirectory(Paths::Get().SavesDirectory()))
        {
            WOC_LOG_ERROR("Cannot create the Saves directory");
            return false;
        }

        const std::string clean = SanitiseName(slotName);

        Json root = Json::MakeObject();
        root["version"] = kFormatVersion;
        root["name"] = clean;
        root["map"] = mapFolder;
        root["day"] = world.Time().TotalDays();
        root["date"] = world.Time().ToString();

        const State* human = const_cast<World&>(world).HumanState();
        root["realm"] = human ? human->name : std::string();
        root["world"] = world.SaveObjects();
        root["fog"] = FogSystem::Get().ToJson();
        WriteProcesses(world, root);

        const std::string path = SlotPath(clean + kExtension);
        if (!root.SaveFile(path, 0))   // compact: saves are data, not documentation
        {
            WOC_LOG_ERROR("Failed to write save ", path);
            return false;
        }
        WOC_LOG_INFO("Saved game to ", path);
        return true;
    }

    namespace
    {
        /// Everything about a party that lives outside the world's own objects: the systems'
        /// work in progress and the players' minds. Written into saves and snapshots alike.
        void WriteProcesses(const World& world, Json& root)
        {
            root["market"] = MarketSystem::Get().ToJson();
            root["forestry"] = ForestrySystem::Get().ToJson();
            root["bandits"] = BanditSystem::Get().ToJson();
            root["battles"] = BattleSystem::Get().ToJson();
            root["roads"] = RoadSystem::Get().ToJson();
            root["diplomacy"] = DiplomacySystem::Get().ToJson();
            root["population"] = PopulationSystem::Get().ToJson();
            root["sim"] = Simulation::Get().ToJson();
            root["random"] = GlobalRandom().State();
            root["dayFraction"] = world.Time().Fraction();

            Json players = Json::MakeArray();
            for (const Scope<IPlayer>& player : world.Players())
            {
                Json node = Json::MakeObject();
                node["state"] = EncodeId(player->StateId());
                node["mind"] = player->ToJson();
                players.Push(node);
            }
            root["players"] = players;
        }

        /// The other half. Players must already exist; their minds are poured back into them.
        void ReadProcesses(World& world, const Json& root)
        {
            MarketSystem::Get().FromJson(root["market"]);
            ForestrySystem::Get().FromJson(root["forestry"]);
            BanditSystem::Get().FromJson(root["bandits"]);
            BattleSystem::Get().FromJson(root["battles"]);
            RoadSystem::Get().Reset();
            RoadSystem::Get().FromJson(root["roads"]);
            RoadSystem::Get().StampExisting(world);
            DiplomacySystem::Get().FromJson(root["diplomacy"]);
            PopulationSystem::Get().FromJson(root["population"]);
            if (root.Has("sim")) Simulation::Get().FromJson(root["sim"]);
            GlobalRandom().SetState(root["random"].AsString());
            world.Time().SetFraction(root["dayFraction"].AsFloat(0.0f));

            for (const Json& node : root["players"].AsArray())
            {
                const EntityId stateId = DecodeId(node["state"]);
                for (const Scope<IPlayer>& player : world.Players())
                {
                    if (player->StateId() == stateId) { player->FromJson(node["mind"]); break; }
                }
            }
        }

        /// One seat per realm: a person's realm is steered by orders, the rest by an AI.
        void SeatPlayers(World& world, bool multiplayer)
        {
            std::vector<EntityId> stateIds;
            for (const auto& [id, state] : world.States()) stateIds.push_back(id);
            std::sort(stateIds.begin(), stateIds.end());
            for (size_t i = 0; i < stateIds.size(); ++i)
            {
                State* state = world.FindState(stateIds[i]);
                if (!state || state->outlaw) continue;   // the robbers answer to BanditSystem
                state->playerControlled = state->id == world.HumanStateId() ||
                                          (multiplayer && !state->peerId.empty());
                if (state->playerControlled) world.AddPlayer(MakeScope<HumanPlayer>(state->id));
                else world.AddPlayer(MakeScope<AIPlayer>(state->id, world.Seed() + static_cast<u32>(i) * 7919u));
            }
        }
    }

    namespace
    {
        const char* kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string EncodeBase64(const std::vector<u8>& bytes)
        {
            std::string out;
            out.reserve((bytes.size() + 2) / 3 * 4);
            for (size_t i = 0; i < bytes.size(); i += 3)
            {
                u32 chunk = static_cast<u32>(bytes[i]) << 16;
                if (i + 1 < bytes.size()) chunk |= static_cast<u32>(bytes[i + 1]) << 8;
                if (i + 2 < bytes.size()) chunk |= bytes[i + 2];
                out += kBase64[(chunk >> 18) & 63];
                out += kBase64[(chunk >> 12) & 63];
                out += i + 1 < bytes.size() ? kBase64[(chunk >> 6) & 63] : '=';
                out += i + 2 < bytes.size() ? kBase64[chunk & 63] : '=';
            }
            return out;
        }

        std::vector<u8> DecodeBase64(const std::string& text)
        {
            i32 lookup[256];
            for (i32& v : lookup) v = -1;
            for (i32 i = 0; i < 64; ++i) lookup[static_cast<u8>(kBase64[i])] = i;

            std::vector<u8> out;
            out.reserve(text.size() / 4 * 3);
            u32 buffer = 0;
            i32 bits = 0;
            for (char c : text)
            {
                const i32 value = lookup[static_cast<u8>(c)];
                if (value < 0) continue;
                buffer = (buffer << 6) | static_cast<u32>(value);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back(static_cast<u8>((buffer >> bits) & 0xFF));
                }
            }
            return out;
        }

        /// What the simulation changes on the ground itself, bit for bit: the density of every
        /// wood and field (the map file keeps them to 1/255, which is fine for a map and fatal
        /// for a lockstep party), the roads and the bridges.
        std::string EncodeTiles(const MapData& map)
        {
            const std::vector<Tile>& tiles = map.Tiles();
            std::vector<u8> bytes(tiles.size() * 12);
            u8* cursor = bytes.data();
            for (const Tile& tile : tiles)
            {
                std::memcpy(cursor, &tile.forest, 4); cursor += 4;
                std::memcpy(cursor, &tile.field, 4); cursor += 4;
                *cursor++ = tile.road;
                *cursor++ = static_cast<u8>((tile.bridged ? 1 : 0) | (tile.fordable ? 2 : 0));
                *cursor++ = tile.owner;
                *cursor++ = tile.holder;
            }
            return EncodeBase64(bytes);
        }

        void DecodeTiles(MapData& map, const std::string& text)
        {
            const std::vector<u8> bytes = DecodeBase64(text);
            std::vector<Tile>& tiles = map.Tiles();
            if (bytes.size() != tiles.size() * 12) return;
            const u8* cursor = bytes.data();
            for (Tile& tile : tiles)
            {
                std::memcpy(&tile.forest, cursor, 4); cursor += 4;
                std::memcpy(&tile.field, cursor, 4); cursor += 4;
                tile.road = *cursor++;
                tile.bridged = (*cursor & 1) != 0;
                tile.fordable = (*cursor & 2) != 0;
                ++cursor;
                tile.owner = *cursor++;
                tile.holder = *cursor++;
            }
        }
    }

    Json SaveGame::Snapshot(const World& world)
    {
        Json root = Json::MakeObject();
        root["world"] = world.SaveObjects(false, true);
        root["tiles"] = EncodeTiles(world.Map());
        root["coverage"] = CoverageSystem::Get().ToJson();
        root["day"] = world.Time().TotalDays();
        WriteProcesses(world, root);
        return root;
    }

    void SaveGame::Restore(World& world, const Json& snapshot, const std::string& localPeer)
    {
        world.ClearPlayers();
        world.AdoptObjects(snapshot["world"], localPeer);
        world.Time().SetTotalDays(snapshot["day"].AsInt(world.Time().TotalDays()));
        SeatPlayers(world, true);
        // Nothing is started afresh: battles, roadworks, embassies, the AI's plans and the
        // dice all carry on exactly where the snapshot caught them.
        ReadProcesses(world, snapshot);
        // The ground last, after the roads were re-stamped: the snapshot's own road grades
        // are the ones that count.
        DecodeTiles(world.MutableMap(), snapshot["tiles"].AsString());
        // The borders as they stood, not as a fresh flood would draw them: the flood starts
        // from the last borders, so recomputing here would already be a different world.
        CoverageSystem::Get().FromJson(snapshot["coverage"]);
        CoverageSystem::Get().RefreshLayer(world);
        ForestrySystem::Get().MarkLayersDirty();
        RoadSystem::Get().MarkDirty();
        WOC_LOG_INFO("Lockstep: world restored at tick ", Simulation::Get().CurrentTick());
    }

    bool SaveGame::Load(World& world, const std::string& fileName, std::string& outMapFolder)
    {
        const std::string path = SlotPath(fileName);
        std::string error;
        const Json root = Json::LoadFile(path, &error);
        if (!error.empty() || root.IsNull())
        {
            WOC_LOG_ERROR("Cannot read save ", path, ": ", error);
            return false;
        }

        outMapFolder = root["map"].AsString("Test");

        world.Reset();
        if (!world.LoadMap(outMapFolder))
        {
            WOC_LOG_ERROR("Save refers to a map that is no longer present: ", outMapFolder);
            return false;
        }

        world.LoadObjects(root["world"]);
        world.Time().SetTotalDays(root["day"].AsInt(0));
        FogSystem::Get().FromJson(root["fog"], world);

        Simulation::Get().Reset();
        SeatPlayers(world, false);
        ReadProcesses(world, root);

        ForestrySystem::Get().ClearUnarableFields(world);
        ForestrySystem::Get().ClearUnforestable(world);
        CoverageSystem::Get().MarkDirty();
        WOC_LOG_INFO("Loaded save ", path, " (", world.Settlements().size(), " settlements)");
        return true;
    }

    std::vector<SaveSlot> SaveGame::List()
    {
        std::vector<SaveSlot> slots;
        for (const std::string& file : Paths::ListFiles(Paths::Get().SavesDirectory(), kExtension))
        {
            const Json root = Json::LoadFile(SlotPath(file));
            if (root.IsNull()) continue;

            SaveSlot slot;
            slot.fileName = file;
            slot.name = root["name"].AsString(file);
            slot.mapFolder = root["map"].AsString();
            slot.realmName = root["realm"].AsString();
            slot.day = root["day"].AsInt(0);
            slot.dateText = root["date"].AsString();
            slots.push_back(std::move(slot));
        }

        // Newest first, by in-game day: that is what a player is looking for.
        std::sort(slots.begin(), slots.end(),
            [](const SaveSlot& a, const SaveSlot& b) { return a.name < b.name; });
        return slots;
    }

    bool SaveGame::Delete(const std::string& fileName)
    {
        return Paths::RemoveFile(SlotPath(fileName));
    }
}
