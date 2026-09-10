// NamePool.h - names.json in memory, with per-race draws.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Random.h"
#include <unordered_map>

namespace woc
{
    class NamePool final : public Singleton<NamePool>
    {
        friend class Singleton<NamePool>;
    public:
        void Load();

        std::string GivenName(const std::string& raceId, bool female, Random& random) const;
        std::string Surname(const std::string& raceId, Random& random) const;
        std::string SettlementName(const std::string& raceId, Random& random);
        std::string ClanName(const std::string& raceId, Random& random);
        std::string StateName(const std::string& raceId, Random& random);
        std::string CohortName(Random& random) const;

        /// Forgets which settlement names have been handed out; call when a party starts.
        void ResetUsed() { m_usedSettlementNames.clear(); m_usedClanNames.clear(); m_usedStateNames.clear(); }

    private:
        NamePool() = default;
        ~NamePool() = default;

        struct RaceNames
        {
            std::vector<std::string> male;
            std::vector<std::string> female;
            std::vector<std::string> surname;
            std::vector<std::string> settlement;
        };

        const RaceNames& For(const std::string& raceId) const;
        static const std::string& Pick(const std::vector<std::string>& pool, Random& random);

        std::unordered_map<std::string, RaceNames> m_races;
        std::unordered_map<std::string, std::vector<std::string>> m_clanNames;
        std::unordered_map<std::string, std::vector<std::string>> m_stateNames;
        std::vector<std::string> m_cohortNames;

        std::vector<std::string> m_usedSettlementNames;
        std::vector<std::string> m_usedClanNames;
        std::vector<std::string> m_usedStateNames;
    };
}
