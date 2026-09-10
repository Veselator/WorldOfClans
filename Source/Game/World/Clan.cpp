#include "Clan.h"

#include <algorithm>

namespace woc
{
    bool Clan::OwnsSettlement(EntityId settlement) const
    {
        return std::find(settlements.begin(), settlements.end(), settlement) != settlements.end();
    }

    void Clan::AddSettlement(EntityId settlement)
    {
        if (!OwnsSettlement(settlement)) settlements.push_back(settlement);
    }

    void Clan::RemoveSettlement(EntityId settlement)
    {
        settlements.erase(std::remove(settlements.begin(), settlements.end(), settlement), settlements.end());
    }

    void Clan::AddCohort(EntityId cohort)
    {
        if (std::find(cohorts.begin(), cohorts.end(), cohort) == cohorts.end()) cohorts.push_back(cohort);
    }

    void Clan::RemoveCohort(EntityId cohort)
    {
        cohorts.erase(std::remove(cohorts.begin(), cohorts.end(), cohort), cohorts.end());
    }

    Json Clan::ToJson() const
    {
        Json node = Json::MakeObject();
        node["id"] = EncodeId(id);
        node["state"] = EncodeId(state);
        node["name"] = name;
        node["race"] = raceId;
        node["faith"] = faithId;
        node["color"] = static_cast<i64>(color.ToRGB());
        node["paletteSlot"] = static_cast<i64>(paletteSlot);
        node["resources"] = resources.ToJson();
        node["settlements"] = EncodeIdList(settlements);
        node["cohorts"] = EncodeIdList(cohorts);
        node["head"] = EncodeId(head);
        node["members"] = EncodeIdList(members);
        node["prestige"] = prestige;
        node["eliminated"] = eliminated;
        return node;
    }

    Clan Clan::FromJson(const Json& node)
    {
        Clan clan;
        clan.id = DecodeId(node["id"]);
        clan.state = DecodeId(node["state"]);
        clan.name = node["name"].AsString();
        clan.raceId = node["race"].AsString("human");
        clan.faithId = node["faith"].AsString("perun");
        clan.color = Color::FromRGB(static_cast<u32>(node["color"].AsNumber(0xC8452D)));
        clan.paletteSlot = static_cast<u8>(node["paletteSlot"].AsInt(1));
        clan.resources = ResourceData::FromJson(node["resources"]);
        clan.settlements = DecodeIdList(node["settlements"]);
        clan.cohorts = DecodeIdList(node["cohorts"]);
        clan.head = DecodeId(node["head"]);
        clan.members = DecodeIdList(node["members"]);
        clan.prestige = node["prestige"].AsFloat(0.0f);
        clan.eliminated = node["eliminated"].AsBool(false);
        return clan;
    }
}
