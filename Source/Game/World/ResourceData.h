// ResourceData.h - the four commodities the whole economy runs on.
#pragma once

#include "../../Core/Math.h"
#include "../../Core/Json.h"

namespace woc
{
    enum class ResourceType : u32 { Money = 0, Wood, Stone, Food, Count };

    struct ResourceData
    {
        f32 money = 0.0f;
        f32 wood = 0.0f;
        f32 stone = 0.0f;
        f32 food = 0.0f;

        f32& operator[](ResourceType type)
        {
            switch (type)
            {
            case ResourceType::Money: return money;
            case ResourceType::Wood:  return wood;
            case ResourceType::Stone: return stone;
            default:                  return food;
            }
        }
        f32 operator[](ResourceType type) const
        {
            return const_cast<ResourceData*>(this)->operator[](type);
        }

        ResourceData operator+(const ResourceData& o) const
        {
            return { money + o.money, wood + o.wood, stone + o.stone, food + o.food };
        }
        ResourceData operator-(const ResourceData& o) const
        {
            return { money - o.money, wood - o.wood, stone - o.stone, food - o.food };
        }
        ResourceData operator*(f32 s) const { return { money * s, wood * s, stone * s, food * s }; }
        ResourceData& operator+=(const ResourceData& o)
        {
            money += o.money; wood += o.wood; stone += o.stone; food += o.food;
            return *this;
        }
        ResourceData& operator-=(const ResourceData& o)
        {
            money -= o.money; wood -= o.wood; stone -= o.stone; food -= o.food;
            return *this;
        }

        /// True when this stock can pay `cost` in full.
        bool CanAfford(const ResourceData& cost) const
        {
            return money >= cost.money && wood >= cost.wood && stone >= cost.stone && food >= cost.food;
        }

        void ClampNonNegative()
        {
            money = std::max(0.0f, money);
            wood = std::max(0.0f, wood);
            stone = std::max(0.0f, stone);
            food = std::max(0.0f, food);
        }

        static ResourceData FromJson(const Json& node)
        {
            return {
                node["money"].AsFloat(0.0f),
                node["wood"].AsFloat(0.0f),
                node["stone"].AsFloat(0.0f),
                node["food"].AsFloat(0.0f)
            };
        }

        Json ToJson() const
        {
            Json out = Json::MakeObject();
            out["money"] = money;
            out["wood"] = wood;
            out["stone"] = stone;
            out["food"] = food;
            return out;
        }

        static const char* Name(ResourceType type)
        {
            switch (type)
            {
            case ResourceType::Money: return "money";
            case ResourceType::Wood:  return "wood";
            case ResourceType::Stone: return "stone";
            default:                  return "food";
            }
        }

        static ResourceType Parse(const std::string& id)
        {
            if (id == "money") return ResourceType::Money;
            if (id == "wood")  return ResourceType::Wood;
            if (id == "stone") return ResourceType::Stone;
            return ResourceType::Food;
        }
    };
}
