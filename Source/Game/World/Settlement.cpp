#include "Settlement.h"

#include <algorithm>

namespace woc
{
    const SettlementTier& Settlement::Tier() const
    {
        const SettlementKindInfo& info = KindInfo();
        static const SettlementTier fallback{};
        if (info.tiers.empty()) return fallback;
        return info.tiers[info.TierForPopulation(population)];
    }

    std::string Settlement::TierName() const
    {
        const SettlementTier& tier = Tier();
        return tier.name.empty() ? KindInfo().name : tier.name;
    }

    bool Settlement::HasBuilding(const std::string& buildingId) const
    {
        return std::find(buildings.begin(), buildings.end(), buildingId) != buildings.end();
    }

    bool Settlement::IsBuilding(const std::string& buildingId) const
    {
        return std::any_of(construction.begin(), construction.end(),
            [&buildingId](const ConstructionOrder& order) { return order.buildingId == buildingId; });
    }

    Json Settlement::ToJson() const
    {
        Json node = Json::MakeObject();
        node["id"] = EncodeId(id);
        node["owner"] = EncodeId(owner);
        node["name"] = name;
        node["kind"] = SettlementDatabase::KindId(kind);
        node["x"] = position.x;
        node["y"] = position.y;
        node["race"] = raceId;
        node["faith"] = faithId;
        node["population"] = population;
        node["prosperity"] = prosperity;
        node["loyalty"] = loyalty;
        node["quarryRevealed"] = quarryRevealed;
        node["rebelliousUntilDay"] = rebelliousUntilDay;

        Json built = Json::MakeArray();
        for (const std::string& building : buildings) built.Push(building);
        node["buildings"] = built;

        if (!construction.empty())
        {
            Json queue = Json::MakeArray();
            for (const ConstructionOrder& order : construction)
            {
                Json item = Json::MakeObject();
                item["building"] = order.buildingId;
                item["daysRemaining"] = order.daysRemaining;
                queue.Push(item);
            }
            node["construction"] = queue;
        }

        if (conversionDaysLeft > 0)
        {
            node["conversionTarget"] = conversionTarget;
            node["conversionDaysLeft"] = conversionDaysLeft;
        }
        return node;
    }

    Settlement Settlement::FromJson(const Json& node)
    {
        Settlement settlement;
        settlement.id = DecodeId(node["id"]);
        settlement.owner = DecodeId(node["owner"]);
        settlement.name = node["name"].AsString();
        settlement.kind = SettlementDatabase::ParseKind(node["kind"].AsString("village"));
        settlement.position = { node["x"].AsFloat(0.0f), node["y"].AsFloat(0.0f) };
        settlement.raceId = node["race"].AsString("human");
        settlement.faithId = node["faith"].AsString("perun");
        settlement.population = node["population"].AsInt(200);
        settlement.prosperity = node["prosperity"].AsFloat(0.5f);
        settlement.loyalty = node["loyalty"].AsFloat(0.75f);
        settlement.quarryRevealed = node["quarryRevealed"].AsBool(false);
        settlement.rebelliousUntilDay = node["rebelliousUntilDay"].AsInt(0);

        for (const Json& building : node["buildings"].AsArray())
            settlement.buildings.push_back(building.AsString());

        for (const Json& item : node["construction"].AsArray())
        {
            ConstructionOrder order;
            order.buildingId = item["building"].AsString();
            order.daysRemaining = item["daysRemaining"].AsInt(0);
            settlement.construction.push_back(std::move(order));
        }

        settlement.conversionTarget = node["conversionTarget"].AsString();
        settlement.conversionDaysLeft = node["conversionDaysLeft"].AsInt(0);
        return settlement;
    }
}
