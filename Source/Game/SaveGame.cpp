#include "SaveGame.h"

#include "Players/AIPlayer.h"
#include "Players/HumanPlayer.h"
#include "Systems/CoverageSystem.h"
#include "Systems/FogSystem.h"
#include "Systems/MarketSystem.h"
#include "Systems/ForestrySystem.h"
#include "Systems/RoadSystem.h"
#include "Systems/Simulation.h"
#include "World/World.h"
#include "../Core/Json.h"
#include "../Core/Log.h"
#include "../Core/Paths.h"

#include <algorithm>

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
        root["market"] = MarketSystem::Get().ToJson();

        const std::string path = SlotPath(clean + kExtension);
        if (!root.SaveFile(path, 0))   // compact: saves are data, not documentation
        {
            WOC_LOG_ERROR("Failed to write save ", path);
            return false;
        }
        WOC_LOG_INFO("Saved game to ", path);
        return true;
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
        MarketSystem::Get().FromJson(root["market"]);

        // Seats are not stored: they follow from which realm the save says is the player's.
        std::vector<EntityId> stateIds;
        for (const auto& [id, state] : world.States()) stateIds.push_back(id);
        std::sort(stateIds.begin(), stateIds.end());

        for (size_t i = 0; i < stateIds.size(); ++i)
        {
            State* state = world.FindState(stateIds[i]);
            if (!state) continue;

            if (state->id == world.HumanStateId())
            {
                state->playerControlled = true;
                world.AddPlayer(MakeScope<HumanPlayer>(state->id));
            }
            else
            {
                state->playerControlled = false;
                world.AddPlayer(MakeScope<AIPlayer>(state->id, world.Seed() + static_cast<u32>(i) * 7919u));
            }
        }

        RoadSystem::Get().Reset();
        RoadSystem::Get().StampExisting(world);
        ForestrySystem::Get().ClearUnarableFields(world);
        Simulation::Get().Reset();
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
