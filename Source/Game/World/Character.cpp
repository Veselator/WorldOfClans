#include "Character.h"
#include "EntityJson.h"

namespace woc
{
    std::string Character::ShortDescription() const
    {
        return FullName() + ", " + std::to_string(age) + " років, " + origin;
    }

    Json Character::ToJson() const
    {
        Json node = Json::MakeObject();
        node["id"] = EncodeId(id);
        node["givenName"] = givenName;
        node["surname"] = surname;
        node["race"] = raceId;
        node["origin"] = origin;
        node["gender"] = gender == Gender::Male ? "male" : "female";
        node["age"] = age;
        node["height"] = height;
        node["weight"] = weight;
        node["wounds"] = wounds;
        node["alive"] = alive;
        node["noble"] = noble;
        node["birthDay"] = birthDay;

        if (!traits.empty())
        {
            Json list = Json::MakeArray();
            for (const std::string& trait : traits) list.Push(trait);
            node["traits"] = list;
        }
        if (noble)
        {
            node["clan"] = EncodeId(clan);
            node["father"] = EncodeId(father);
            node["mother"] = EncodeId(mother);
            node["spouse"] = EncodeId(spouse);
            Json kids = Json::MakeArray();
            for (EntityId child : children) kids.Push(EncodeId(child));
            node["children"] = kids;
        }
        return node;
    }

    Character Character::FromJson(const Json& node)
    {
        Character character;
        character.id = DecodeId(node["id"]);
        character.givenName = node["givenName"].AsString();
        character.surname = node["surname"].AsString();
        character.raceId = node["race"].AsString("human");
        character.origin = node["origin"].AsString();
        character.gender = node["gender"].AsString("male") == "female" ? Gender::Female : Gender::Male;
        character.age = node["age"].AsInt(20);
        character.height = node["height"].AsFloat(175.0f);
        character.weight = node["weight"].AsFloat(74.0f);
        character.wounds = node["wounds"].AsFloat(0.0f);
        character.alive = node["alive"].AsBool(true);
        character.noble = node["noble"].AsBool(false);
        character.birthDay = node["birthDay"].AsInt(0);

        for (const Json& trait : node["traits"].AsArray()) character.traits.push_back(trait.AsString());

        if (character.noble)
        {
            character.clan = DecodeId(node["clan"]);
            character.father = DecodeId(node["father"]);
            character.mother = DecodeId(node["mother"]);
            character.spouse = DecodeId(node["spouse"]);
            for (const Json& child : node["children"].AsArray())
                character.children.push_back(DecodeId(child));
        }
        return character;
    }
}
