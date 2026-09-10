#include "SettlementDatabase.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

namespace woc
{
    namespace
    {
        const char* kKindIds[] = { "village", "city", "castle" };

        SpriteId ParseSprite(const std::string& name)
        {
            if (name == "City") return SpriteId::City;
            if (name == "Castle") return SpriteId::Castle;
            return SpriteId::Village;
        }
    }

    const char* SettlementDatabase::KindId(SettlementKind kind)
    {
        const size_t index = static_cast<size_t>(kind);
        return index < 3 ? kKindIds[index] : kKindIds[0];
    }

    SettlementKind SettlementDatabase::ParseKind(const std::string& id)
    {
        if (id == "city") return SettlementKind::City;
        if (id == "castle") return SettlementKind::Castle;
        return SettlementKind::Village;
    }

    size_t SettlementKindInfo::TierForPopulation(i32 population) const
    {
        size_t best = 0;
        for (size_t i = 0; i < tiers.size(); ++i)
        {
            if (population >= tiers[i].minPopulation) best = i;
        }
        return best;
    }

    void SettlementDatabase::Load()
    {
        m_kinds.clear();
        const Json& doc = ConfigManager::Get().Settlements();

        for (const Json& entry : doc["kinds"].AsArray())
        {
            SettlementKindInfo info;
            info.id = entry["id"].AsString();
            info.kind = ParseKind(info.id);
            info.name = entry["name"].AsString(info.id);
            info.sprite = ParseSprite(entry["sprite"].AsString());

            const std::string resource = entry["baseResource"].AsString("food");
            info.producesResource = resource != "none";
            info.baseResource = ResourceData::Parse(resource);

            info.canBeIndependent = entry["canBeIndependent"].AsBool(false);
            info.buildCost = ResourceData::FromJson(entry["buildCost"]);
            info.buildDays = entry["buildDays"].AsInt(60);

            for (const Json& tierNode : entry["tiers"].AsArray())
            {
                SettlementTier tier;
                tier.tier = tierNode["tier"].AsInt(1);
                tier.name = tierNode["name"].AsString();
                tier.minPopulation = tierNode["minPopulation"].AsInt(0);
                tier.maxPopulation = tierNode["maxPopulation"].AsInt(1000);
                tier.coverage = tierNode["coverage"].AsFloat(0.0f);
                tier.production = tierNode["production"].AsFloat(1.0f);
                tier.garrison = tierNode["garrison"].AsInt(1);
                tier.prosperityCap = tierNode["prosperityCap"].AsFloat(1.0f);
                info.tiers.push_back(std::move(tier));
            }

            m_kinds.push_back(std::move(info));
        }

        m_startingProsperity = doc.GetFloat("defaults/startingProsperity", 0.5f);
        m_startingLoyalty = doc.GetFloat("defaults/startingLoyalty", 0.75f);
        m_defenseBase = doc.GetFloat("defaults/defenseBase", 1.0f);
        m_siegeSupplyDays = doc.GetFloat("defaults/siegeSupplyDays", 90.0f);
        m_minDistance = doc.GetFloat("defaults/minDistanceBetween", 70.0f);
        m_minDistanceVillage = doc.GetFloat("defaults/minDistanceVillage", 46.0f);
        m_actions = doc["actions"];

        WOC_LOG_INFO("Settlement database: ", m_kinds.size(), " kinds");
    }

    const SettlementKindInfo& SettlementDatabase::Kind(SettlementKind kind) const
    {
        for (const SettlementKindInfo& info : m_kinds)
        {
            if (info.kind == kind) return info;
        }
        static const SettlementKindInfo fallback{};
        return m_kinds.empty() ? fallback : m_kinds.front();
    }

    const Json& SettlementDatabase::Action(const std::string& id) const
    {
        return m_actions[id];
    }
}
