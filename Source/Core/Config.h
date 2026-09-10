// Config.h - central registry of every JSON configuration document.
//
// Nothing in the game hard-codes balance numbers: gameplay code asks the
// ConfigManager for a document and reads values by path. Files are hot-reloadable
// (see Reload) which makes tuning a matter of editing JSON and pressing F5.
#pragma once

#include "Singleton.h"
#include "Json.h"
#include <unordered_map>

namespace woc
{
    class ConfigManager final : public Singleton<ConfigManager>
    {
        friend class Singleton<ConfigManager>;
    public:
        /// Loads the standard document set from <root>/Config.
        void LoadAll();
        /// Re-reads every document already registered; used by the F5 hot-reload key.
        void Reload();

        /// Returns a named document, loading it on first use. Missing files yield a null Json.
        const Json& Document(const std::string& name);

        const Json& Game()      { return Document("game"); }
        const Json& Units()     { return Document("units"); }
        const Json& Terrain()   { return Document("terrain"); }
        const Json& Buildings() { return Document("buildings"); }
        const Json& Races()     { return Document("races"); }
        const Json& Names()     { return Document("names"); }
        const Json& Settlements(){ return Document("settlements"); }
        const Json& UI()        { return Document("ui"); }

        /// Shorthand for Game() lookups such as Value("camera/zoom/min").
        f32 Float(const std::string& path, f32 fallback = 0.0f) { return Game().GetFloat(path, fallback); }
        i32 Int(const std::string& path, i32 fallback = 0) { return Game().GetInt(path, fallback); }
        bool Bool(const std::string& path, bool fallback = false) { return Game().GetBool(path, fallback); }
        std::string Str(const std::string& path, const std::string& fallback = "")
        {
            return Game().GetString(path, fallback);
        }

    private:
        ConfigManager() = default;
        ~ConfigManager() = default;

        std::unordered_map<std::string, Json> m_documents;
    };
}
