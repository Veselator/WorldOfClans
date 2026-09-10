#include "BuildingDatabase.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        DecoratorKind ParseDecorator(const std::string& id)
        {
            if (id == "defense")  return DecoratorKind::Defense;
            if (id == "military") return DecoratorKind::Military;
            if (id == "loyalty")  return DecoratorKind::Loyalty;
            if (id == "coverage") return DecoratorKind::Coverage;
            return DecoratorKind::Production;
        }
    }

    bool BuildingInfo::AllowedFor(const std::string& kindId) const
    {
        return std::find(allowedKinds.begin(), allowedKinds.end(), kindId) != allowedKinds.end();
    }

    void BuildingDatabase::Load()
    {
        m_buildings.clear();
        const Json& doc = ConfigManager::Get().Buildings();

        for (const Json& entry : doc["buildings"].AsArray())
        {
            BuildingInfo info;
            info.id = entry["id"].AsString();
            info.name = entry["name"].AsString(info.id);
            info.description = entry["description"].AsString();
            for (const Json& kind : entry["allowedKinds"].AsArray())
                info.allowedKinds.push_back(kind.AsString());

            info.cost = ResourceData::FromJson(entry["cost"]);
            info.buildDays = entry["buildDays"].AsInt(30);
            info.decorator = ParseDecorator(entry["decorator"].AsString("production"));

            const Json& requiresNode = entry["requires"];
            info.requirement.forestNearby = requiresNode["forestNearby"].AsFloat(0.0f);
            info.requirement.stoneNearby = requiresNode["stoneNearby"].AsFloat(0.0f);
            info.requirement.coastNearby = requiresNode["coastNearby"].AsFloat(0.0f);
            info.requirement.waterNearby = requiresNode["waterNearby"].AsFloat(0.0f);
            info.requirement.requiresBuilding = entry["requiresBuilding"].AsString();

            const Json& effect = entry["effect"];
            info.resource = ResourceData::Parse(effect["resource"].AsString("food"));
            info.multiplier = effect["multiplier"].AsFloat(1.0f);
            info.flat = effect["flat"].AsFloat(0.0f);
            info.defenseMultiplier = effect["defenseMultiplier"].AsFloat(1.0f);
            info.trainingBonus = effect["trainingBonus"].AsFloat(0.0f);
            info.recruitCostMultiplier = effect["recruitCostMultiplier"].AsFloat(1.0f);
            info.cavalryCostMultiplier = effect["cavalryCostMultiplier"].AsFloat(1.0f);
            info.moraleBonus = effect["moraleBonus"].AsFloat(0.0f);
            info.loyaltyPerMonth = effect["loyaltyPerMonth"].AsFloat(0.0f);
            info.conversionCostMultiplier = effect["conversionCostMultiplier"].AsFloat(1.0f);
            info.distancePenaltyMultiplier = effect["distancePenaltyMultiplier"].AsFloat(1.0f);
            info.coverageMultiplier = effect["coverageMultiplier"].AsFloat(1.0f);

            const Json& extra = entry["extra"];
            info.caravanBonus = extra["caravanBonus"].AsFloat(effect["caravanBonus"].AsFloat(0.0f));
            info.forestHarvest = extra["forestHarvest"].AsFloat(0.0f);
            info.fieldRadiusBonus = extra["fieldRadiusBonus"].AsFloat(0.0f);
            info.siegeSupplyDays = extra["siegeSupplyDays"].AsFloat(0.0f);
            info.bridgeRadius = extra["bridgeRadius"].AsFloat(0.0f);
            info.bridgesWater = extra["bridgesWater"].AsBool(false);
            info.revealsQuarrySprite = extra["revealsQuarrySprite"].AsBool(false);

            m_buildings.push_back(std::move(info));
        }

        WOC_LOG_INFO("Building database: ", m_buildings.size(), " buildings");
    }

    const BuildingInfo* BuildingDatabase::Find(const std::string& id) const
    {
        for (const BuildingInfo& info : m_buildings)
        {
            if (info.id == id) return &info;
        }
        return nullptr;
    }
}
