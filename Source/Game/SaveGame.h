// SaveGame.h - named save slots.
//
// A save is the whole world dump plus the few things that live outside it: which map it
// was played on, what day it is, and who sits in which seat.
#pragma once

#include "../Core/Types.h"
#include "../Core/Json.h"

namespace woc
{
    class World;

    struct SaveSlot
    {
        std::string name;        // slot name as shown to the player
        std::string fileName;    // file inside Saves/
        std::string mapFolder;
        std::string realmName;
        i32 day = 0;
        std::string dateText;
    };

    class SaveGame
    {
    public:
        /// Writes the world into Saves/<name>.wocsave, overwriting an existing slot.
        static bool Save(const World& world, const std::string& slotName, const std::string& mapFolder);
        /// Restores a save and rebuilds the player seats. Returns the map folder on success.
        static bool Load(World& world, const std::string& fileName, std::string& outMapFolder);

        /// The whole world in memory, for a machine that has fallen out of step.
        static Json Snapshot(const World& world);
        /// Puts that world in place of this one - on the host and on the clients alike, so
        /// that afterwards all of them are exactly equal. `localPeer` picks this machine's
        /// realm; the map itself is left as it is.
        static void Restore(World& world, const Json& snapshot, const std::string& localPeer);

        static std::vector<SaveSlot> List();
        static bool Delete(const std::string& fileName);

        /// Turns a player-typed name into a safe file name.
        static std::string SanitiseName(const std::string& name);
    };
}
