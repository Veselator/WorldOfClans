#include "RaceDatabase.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <cstdlib>

namespace woc
{
    namespace
    {
        Color ParseColor(const std::string& hex)
        {
            return Color::FromRGB(static_cast<u32>(std::strtoul(hex.c_str(), nullptr, 16)));
        }

        SpriteId ParseSprite(const std::string& name)
        {
            if (name == "Elf") return SpriteId::Elf;
            if (name == "Dwarf") return SpriteId::Dwarf;
            return SpriteId::Human;
        }
    }

    void RaceDatabase::Load()
    {
        m_races.clear();
        m_faiths.clear();
        const Json& doc = ConfigManager::Get().Races();

        for (const Json& entry : doc["races"].AsArray())
        {
            RaceInfo info;
            info.id = entry["id"].AsString();
            info.name = entry["name"].AsString(info.id);
            info.description = entry["description"].AsString();
            info.sprite = ParseSprite(entry["sprite"].AsString());
            info.color = ParseColor(entry["color"].AsString("ffffff"));
            info.defaultFaith = entry["defaultFaith"].AsString();
            info.forestAffinity = entry["forestAffinity"].AsFloat(1.0f);
            info.hillAffinity = entry["hillAffinity"].AsFloat(1.0f);

            for (const Json& faith : entry["faiths"].AsArray()) info.faiths.push_back(faith.AsString());
            for (const Json& terrain : entry["preferredTerrain"].AsArray())
                info.preferredTerrain.push_back(terrain.AsString());

            const Json& modifiers = entry["modifiers"];
            info.modifiers.populationGrowth = modifiers["populationGrowth"].AsFloat(1.0f);
            info.modifiers.foodProduction = modifiers["foodProduction"].AsFloat(1.0f);
            info.modifiers.moneyProduction = modifiers["moneyProduction"].AsFloat(1.0f);
            info.modifiers.woodProduction = modifiers["woodProduction"].AsFloat(1.0f);
            info.modifiers.stoneProduction = modifiers["stoneProduction"].AsFloat(1.0f);
            info.modifiers.coverage = modifiers["coverage"].AsFloat(1.0f);
            info.modifiers.loyalty = modifiers["loyalty"].AsFloat(1.0f);
            info.modifiers.recruitCost = modifiers["recruitCost"].AsFloat(1.0f);

            m_races.push_back(std::move(info));
        }

        for (const Json& entry : doc["faiths"].AsArray())
        {
            FaithInfo faith;
            faith.id = entry["id"].AsString();
            faith.name = entry["name"].AsString(faith.id);
            faith.color = ParseColor(entry["color"].AsString("ffffff"));
            faith.loyaltyBonus = entry["loyaltyBonus"].AsFloat(0.0f);
            faith.moraleBonus = entry["moraleBonus"].AsFloat(0.0f);
            m_faiths.push_back(std::move(faith));
        }

        m_conversionBaseCost = doc.GetFloat("conversion/baseCost", 420.0f);
        m_conversionCostPerPopulation = doc.GetFloat("conversion/costPerPopulation", 0.9f);
        m_conversionDays = doc.GetInt("conversion/durationDays", 120);
        m_conversionLoyaltyShock = doc.GetFloat("conversion/loyaltyShock", 0.25f);
        m_conversionSameRaceDiscount = doc.GetFloat("conversion/sameRaceDiscount", 0.7f);

        WOC_LOG_INFO("Race database: ", m_races.size(), " races, ", m_faiths.size(), " faiths");
    }

    const RaceInfo& RaceDatabase::Race(const std::string& id) const
    {
        for (const RaceInfo& info : m_races)
        {
            if (info.id == id) return info;
        }
        static const RaceInfo fallback{};
        return m_races.empty() ? fallback : m_races.front();
    }

    const FaithInfo& RaceDatabase::Faith(const std::string& id) const
    {
        for (const FaithInfo& info : m_faiths)
        {
            if (info.id == id) return info;
        }
        static const FaithInfo fallback{};
        return m_faiths.empty() ? fallback : m_faiths.front();
    }
}
