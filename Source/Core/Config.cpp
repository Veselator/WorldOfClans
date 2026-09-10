#include "Config.h"
#include "Paths.h"
#include "Log.h"
#include "Random.h"

namespace woc
{
    Random& GlobalRandom()
    {
        static Random s_random(1337u);
        return s_random;
    }

    void ConfigManager::LoadAll()
    {
        static const char* kDocuments[] = {
            "game", "units", "terrain", "buildings", "races", "names", "settlements", "ui"
        };
        for (const char* name : kDocuments) Document(name);
    }

    void ConfigManager::Reload()
    {
        std::vector<std::string> names;
        names.reserve(m_documents.size());
        for (const auto& [name, doc] : m_documents) names.push_back(name);
        m_documents.clear();
        for (const std::string& name : names) Document(name);
        WOC_LOG_INFO("Reloaded ", names.size(), " configuration documents");
    }

    const Json& ConfigManager::Document(const std::string& name)
    {
        const auto it = m_documents.find(name);
        if (it != m_documents.end()) return it->second;

        const std::string path = Paths::Get().Config(name + ".json");
        std::string error;
        Json doc = Json::LoadFile(path, &error);
        if (!error.empty())
        {
            WOC_LOG_ERROR("Config '", name, "' failed to load: ", error);
        }
        else
        {
            WOC_LOG_TRACE("Loaded config '", name, "' (", doc.Size(), " entries)");
        }
        return m_documents.emplace(name, std::move(doc)).first->second;
    }
}
