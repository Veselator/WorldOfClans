#include "NamePool.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        std::vector<std::string> ReadList(const Json& node)
        {
            std::vector<std::string> list;
            for (const Json& entry : node.AsArray()) list.push_back(entry.AsString());
            return list;
        }
    }

    void NamePool::Load()
    {
        m_races.clear();
        m_clanNames.clear();
        m_stateNames.clear();
        m_cohortNames.clear();
        ResetUsed();

        const Json& doc = ConfigManager::Get().Names();

        for (const char* raceId : { "human", "elf", "dwarf" })
        {
            const Json& node = doc[raceId];
            RaceNames names;
            names.male = ReadList(node["male"]);
            names.female = ReadList(node["female"]);
            names.surname = ReadList(node["surname"]);
            names.settlement = ReadList(node["settlement"]);
            m_races.emplace(raceId, std::move(names));
        }

        for (const auto& [raceId, list] : doc["clanNames"].AsObject()) m_clanNames[raceId] = ReadList(list);
        for (const auto& [raceId, list] : doc["stateNames"].AsObject()) m_stateNames[raceId] = ReadList(list);
        m_cohortNames = ReadList(doc["cohortNames"]);

        WOC_LOG_INFO("Name pool loaded for ", m_races.size(), " races");
    }

    const NamePool::RaceNames& NamePool::For(const std::string& raceId) const
    {
        const auto it = m_races.find(raceId);
        if (it != m_races.end()) return it->second;
        static const RaceNames fallback{};
        const auto human = m_races.find("human");
        return human != m_races.end() ? human->second : fallback;
    }

    const std::string& NamePool::Pick(const std::vector<std::string>& pool, Random& random)
    {
        static const std::string empty = "Безіменний";
        if (pool.empty()) return empty;
        return pool[static_cast<size_t>(random.Range(0, static_cast<i32>(pool.size()) - 1))];
    }

    std::string NamePool::GivenName(const std::string& raceId, bool female, Random& random) const
    {
        const RaceNames& names = For(raceId);
        return Pick(female ? names.female : names.male, random);
    }

    std::string NamePool::Surname(const std::string& raceId, Random& random) const
    {
        return Pick(For(raceId).surname, random);
    }

    std::string NamePool::SettlementName(const std::string& raceId, Random& random)
    {
        const RaceNames& names = For(raceId);
        if (names.settlement.empty()) return "Поселення";

        // Prefer an unused name; once the pool runs dry, start numbering.
        for (int attempt = 0; attempt < 24; ++attempt)
        {
            const std::string& candidate = Pick(names.settlement, random);
            if (std::find(m_usedSettlementNames.begin(), m_usedSettlementNames.end(), candidate) ==
                m_usedSettlementNames.end())
            {
                m_usedSettlementNames.push_back(candidate);
                return candidate;
            }
        }
        const std::string base = Pick(names.settlement, random);
        const std::string numbered = base + " Новий";
        m_usedSettlementNames.push_back(numbered);
        return numbered;
    }

    std::string NamePool::ClanName(const std::string& raceId, Random& random)
    {
        const auto it = m_clanNames.find(raceId);
        if (it == m_clanNames.end() || it->second.empty()) return "Безіменний рід";

        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const std::string& candidate = Pick(it->second, random);
            if (std::find(m_usedClanNames.begin(), m_usedClanNames.end(), candidate) == m_usedClanNames.end())
            {
                m_usedClanNames.push_back(candidate);
                return candidate;
            }
        }
        return Pick(it->second, random);
    }

    std::string NamePool::StateName(const std::string& raceId, Random& random)
    {
        const auto it = m_stateNames.find(raceId);
        if (it == m_stateNames.end() || it->second.empty()) return "Держава";

        // Two realms with the same name would be unreadable on the diplomacy screen.
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const std::string& candidate = Pick(it->second, random);
            if (std::find(m_usedStateNames.begin(), m_usedStateNames.end(), candidate) ==
                m_usedStateNames.end())
            {
                m_usedStateNames.push_back(candidate);
                return candidate;
            }
        }

        // The pool is exhausted: distinguish by the seat of the ruling house.
        const std::string base = Pick(it->second, random);
        const std::string numbered = base + " (" + std::to_string(m_usedStateNames.size() + 1) + ")";
        m_usedStateNames.push_back(numbered);
        return numbered;
    }

    std::string NamePool::CohortName(Random& random) const
    {
        return Pick(m_cohortNames, random);
    }
}
