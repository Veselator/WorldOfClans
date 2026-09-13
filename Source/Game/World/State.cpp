#include "State.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        const Relation& NeutralRelation()
        {
            static const Relation s_neutral;
            return s_neutral;
        }
    }

    const char* State::StanceName(DiplomaticStance stance)
    {
        switch (stance)
        {
        case DiplomaticStance::Neutral:       return "Нейтралітет";
        case DiplomaticStance::NonAggression: return "Пакт про ненапад";
        case DiplomaticStance::Alliance:      return "Союз";
        case DiplomaticStance::War:           return "Війна";
        case DiplomaticStance::Truce:         return "Перемир'я";
        }
        return "?";
    }

    Relation& State::RelationWith(EntityId other)
    {
        return relations[other];
    }

    const Relation& State::RelationWith(EntityId other) const
    {
        const auto it = relations.find(other);
        return it == relations.end() ? NeutralRelation() : it->second;
    }

    DiplomaticStance State::StanceWith(EntityId other) const
    {
        if (other == id) return DiplomaticStance::Alliance;
        return RelationWith(other).stance;
    }

    void State::AddClan(EntityId clan)
    {
        if (std::find(clans.begin(), clans.end(), clan) == clans.end()) clans.push_back(clan);
        if (leader == kInvalidId) leader = clan;
    }

    void State::RemoveClan(EntityId clan)
    {
        clans.erase(std::remove(clans.begin(), clans.end(), clan), clans.end());
        if (leader == clan) leader = clans.empty() ? kInvalidId : clans.front();
    }

    bool State::HasMet(EntityId other) const
    {
        return other == id || std::find(met.begin(), met.end(), other) != met.end();
    }

    Json State::ToJson() const
    {
        Json node = Json::MakeObject();
        node["id"] = EncodeId(id);
        node["name"] = name;
        node["race"] = raceId;
        node["color"] = static_cast<i64>(color.ToRGB());
        node["clans"] = EncodeIdList(clans);
        node["leader"] = EncodeId(leader);
        node["playerControlled"] = playerControlled;
        if (outlaw) node["outlaw"] = true;
        if (!peerId.empty()) node["peer"] = peerId;
        node["eliminated"] = eliminated;

        Json list = Json::MakeArray();
        for (const auto& [other, relation] : relations)
        {
            Json entry = Json::MakeObject();
            entry["with"] = EncodeId(other);
            entry["stance"] = static_cast<i64>(relation.stance);
            entry["opinion"] = relation.opinion;
            entry["until"] = relation.stanceUntilDay;
            list.Push(entry);
        }
        node["relations"] = list;
        node["met"] = EncodeIdList(met);
        return node;
    }

    State State::FromJson(const Json& node)
    {
        State state;
        state.id = DecodeId(node["id"]);
        state.name = node["name"].AsString();
        state.raceId = node["race"].AsString("human");
        state.color = Color::FromRGB(static_cast<u32>(node["color"].AsNumber(0xC8452D)));
        state.clans = DecodeIdList(node["clans"]);
        state.leader = DecodeId(node["leader"]);
        state.playerControlled = node["playerControlled"].AsBool(false);
        state.outlaw = node["outlaw"].AsBool(false);
        state.peerId = node["peer"].AsString();
        state.eliminated = node["eliminated"].AsBool(false);
        state.met = DecodeIdList(node["met"]);

        for (const Json& entry : node["relations"].AsArray())
        {
            Relation relation;
            relation.stance = static_cast<DiplomaticStance>(entry["stance"].AsInt(0));
            relation.opinion = entry["opinion"].AsFloat(0.0f);
            relation.stanceUntilDay = entry["until"].AsInt(0);
            state.relations[DecodeId(entry["with"])] = relation;
        }
        return state;
    }
}
