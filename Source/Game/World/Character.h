// Character.h - one named person.
//
// The same type serves a spearman in the third rank and the prince who commands him:
// everybody has a name, an age, a height, a birthplace. Nobles additionally carry the
// dynastic links that marriage, inheritance and succession run on.
#pragma once

#include "../../Core/Types.h"
#include "../../Core/Math.h"
#include "../../Core/Json.h"

namespace woc
{
    enum class Gender : u8 { Male, Female };

    struct Trait
    {
        std::string id;
        std::string name;
        f32 attack = 0.0f;
        f32 defense = 0.0f;
        f32 health = 0.0f;
        f32 morale = 0.0f;
        f32 training = 0.0f;
    };

    class Character
    {
    public:
        EntityId id = kInvalidId;

        std::string givenName;
        std::string surname;
        std::string raceId;
        std::string origin;          // settlement the character was born in
        Gender gender = Gender::Male;

        i32 age = 20;
        f32 height = 175.0f;         // centimetres
        f32 weight = 74.0f;          // kilograms

        f32 wounds = 0.0f;           // 0 = healthy, 1 = dead
        bool alive = true;

        std::vector<std::string> traits;

        // --- dynasty ---------------------------------------------------------------------
        bool noble = false;
        EntityId clan = kInvalidId;
        EntityId father = kInvalidId;
        EntityId mother = kInvalidId;
        EntityId spouse = kInvalidId;
        std::vector<EntityId> children;
        i32 birthDay = 0;            // absolute simulation day, for age bookkeeping

        std::string FullName() const
        {
            return surname.empty() ? givenName : givenName + " " + surname;
        }

        /// Compact one-line summary used in list rows.
        std::string ShortDescription() const;

        Json ToJson() const;
        static Character FromJson(const Json& node);
    };
}
