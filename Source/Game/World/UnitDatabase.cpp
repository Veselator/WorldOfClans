#include "UnitDatabase.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <iterator>

namespace woc
{
    namespace
    {
        const char* kRoleIds[] = { "aristocrat", "swordsman", "archer", "cavalry", "horseArcher" };
    }

    const char* UnitDatabase::RoleId(UnitRole role)
    {
        const size_t index = static_cast<size_t>(role);
        return index < std::size(kRoleIds) ? kRoleIds[index] : kRoleIds[1];
    }

    UnitRole UnitDatabase::ParseRole(const std::string& id)
    {
        for (size_t i = 0; i < std::size(kRoleIds); ++i)
        {
            if (id == kRoleIds[i]) return static_cast<UnitRole>(i);
        }
        return UnitRole::Swordsman;
    }

    void UnitDatabase::Load()
    {
        m_roles.clear();
        m_unitsByRace.clear();
        m_counters.clear();
        m_terrainAffinity.clear();
        m_unitSizeByTier.clear();
        m_heightRanges.clear();
        m_weightFactors.clear();
        m_traits.clear();

        const Json& doc = ConfigManager::Get().Units();

        m_maxUnits = static_cast<u32>(doc.GetInt("cohort/maxUnits", 10));
        m_maxCharacters = static_cast<u32>(doc.GetInt("cohort/maxCharactersPerUnit", 100));
        m_baseMorale = doc.GetFloat("cohort/baseMorale", 0.75f);
        m_baseTraining = doc.GetFloat("cohort/baseTraining", 0.35f);
        m_trainingPerMonth = doc.GetFloat("cohort/trainingPerMonthInSettlement", 0.02f);
        m_experiencePerBattle = doc.GetFloat("cohort/experienceGainPerBattle", 0.02f);
        m_experienceCombatWeight = doc.GetFloat("cohort/experienceCombatWeight", 0.35f);
        m_minLoyaltyToRecruit = doc.GetFloat("recruitment/minLoyaltyToRecruit", 0.2f);
        m_loyaltyCostPerUnit = doc.GetFloat("recruitment/loyaltyCostPerUnit", 0.01f);

        for (const Json& entry : doc["roles"].AsArray())
        {
            RoleInfo info;
            info.id = entry["id"].AsString();
            info.role = ParseRole(info.id);
            info.name = entry["name"].AsString(info.id);
            info.description = entry["description"].AsString();
            info.single = entry["single"].AsBool(false);
            m_roles.push_back(std::move(info));
        }

        for (const auto& [attacker, row] : doc["counters"].AsObject())
        {
            for (const auto& [defender, value] : row.AsObject())
            {
                m_counters[attacker][defender] = value.AsFloat(1.0f);
            }
        }

        for (const auto& [roleId, row] : doc["terrainAffinity"].AsObject())
        {
            if (!row.IsObject()) continue;
            for (const auto& [terrainId, value] : row.AsObject())
            {
                m_terrainAffinity[roleId][terrainId] = value.AsFloat(1.0f);
            }
        }

        for (const auto& [raceId, raceNode] : doc["races"].AsObject())
        {
            std::vector<UnitData> units;
            for (const Json& entry : raceNode["units"].AsArray())
            {
                UnitData data;
                data.roleId = entry["role"].AsString("swordsman");
                data.role = ParseRole(data.roleId);
                data.raceId = raceId;
                data.name = entry["name"].AsString(data.roleId);
                data.attack = entry["attack"].AsFloat(10.0f);
                data.defense = entry["defense"].AsFloat(10.0f);
                data.health = entry["health"].AsFloat(20.0f);
                data.speed = entry["speed"].AsFloat(1.0f);
                data.range = entry["range"].AsFloat(1.0f);
                data.recruitCost = entry["recruitCost"].AsFloat(20.0f);
                data.upkeep = entry["upkeep"].AsFloat(0.15f);
                data.trainDays = entry["trainDays"].AsFloat(60.0f);
                data.raiseHours = entry["raiseHours"].AsFloat(
                    doc.GetFloat("recruitment/raiseHours/" + data.roleId, 24.0f));
                data.morale = entry["morale"].AsFloat(0.75f);
                data.commandBonus = entry["commandBonus"].AsFloat(0.0f);
                units.push_back(std::move(data));
            }
            m_unitsByRace.emplace(raceId, std::move(units));
        }

        for (const auto& [tier, value] : doc.Get("recruitment/unitSizeByTier").AsObject())
        {
            m_unitSizeByTier[tier] = static_cast<u32>(value.AsInt(50));
        }

        for (const Json& entry : doc["characterTraits"].AsArray())
        {
            Trait trait;
            trait.id = entry["id"].AsString();
            trait.name = entry["name"].AsString(trait.id);
            trait.attack = entry["attack"].AsFloat(0.0f);
            trait.defense = entry["defense"].AsFloat(0.0f);
            trait.health = entry["health"].AsFloat(0.0f);
            trait.morale = entry["morale"].AsFloat(0.0f);
            trait.training = entry["training"].AsFloat(0.0f);
            m_traits.push_back(std::move(trait));
        }

        for (const auto& [raceId, range] : doc["heightRange"].AsObject())
        {
            m_heightRanges[raceId] = { range["min"].AsFloat(160.0f), range["max"].AsFloat(190.0f) };
        }
        for (const auto& [raceId, factor] : doc["weightFactor"].AsObject())
        {
            m_weightFactors[raceId] = factor.AsFloat(0.42f);
        }
        m_ageRange = { doc.GetFloat("ageRange/min", 16.0f), doc.GetFloat("ageRange/max", 52.0f) };

        WOC_LOG_INFO("Unit database: ", m_unitsByRace.size(), " races, ", m_roles.size(), " roles");
    }

    const RoleInfo& UnitDatabase::Role(UnitRole role) const
    {
        for (const RoleInfo& info : m_roles)
        {
            if (info.role == role) return info;
        }
        static const RoleInfo fallback{};
        return m_roles.empty() ? fallback : m_roles.front();
    }

    bool UnitDatabase::HasRace(const std::string& raceId) const
    {
        return m_unitsByRace.find(raceId) != m_unitsByRace.end();
    }

    const UnitData& UnitDatabase::Stats(const std::string& raceId, UnitRole role) const
    {
        static const UnitData fallback{};

        auto it = m_unitsByRace.find(raceId);
        if (it == m_unitsByRace.end()) it = m_unitsByRace.find("human");
        if (it == m_unitsByRace.end() || it->second.empty()) return fallback;

        for (const UnitData& data : it->second)
        {
            if (data.role == role) return data;
        }
        return it->second.front();
    }

    f32 UnitDatabase::Counter(UnitRole attacker, UnitRole defender) const
    {
        const auto row = m_counters.find(RoleId(attacker));
        if (row == m_counters.end()) return 1.0f;
        const auto cell = row->second.find(RoleId(defender));
        return cell == row->second.end() ? 1.0f : cell->second;
    }

    f32 UnitDatabase::TerrainAffinity(UnitRole role, const std::string& terrainId) const
    {
        const auto row = m_terrainAffinity.find(RoleId(role));
        if (row == m_terrainAffinity.end()) return 1.0f;
        const auto cell = row->second.find(terrainId);
        return cell == row->second.end() ? 1.0f : cell->second;
    }

    const Trait* UnitDatabase::FindTrait(const std::string& id) const
    {
        for (const Trait& trait : m_traits)
        {
            if (trait.id == id) return &trait;
        }
        return nullptr;
    }

    u32 UnitDatabase::UnitSizeForTier(const std::string& tierKey) const
    {
        const auto it = m_unitSizeByTier.find(tierKey);
        return it == m_unitSizeByTier.end() ? 50u : it->second;
    }

    Vec2 UnitDatabase::HeightRange(const std::string& raceId) const
    {
        const auto it = m_heightRanges.find(raceId);
        return it == m_heightRanges.end() ? Vec2{ 160.0f, 190.0f } : it->second;
    }

    f32 UnitDatabase::WeightFactor(const std::string& raceId) const
    {
        const auto it = m_weightFactors.find(raceId);
        return it == m_weightFactors.end() ? 0.42f : it->second;
    }
}
