// RaceDatabase.h - races.json in memory: the three peoples and their faiths.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"
#include "../../Render/RenderTypes.h"
#include <unordered_map>

namespace woc
{
    struct RaceModifiers
    {
        f32 populationGrowth = 1.0f;
        f32 foodProduction = 1.0f;
        f32 moneyProduction = 1.0f;
        f32 woodProduction = 1.0f;
        f32 stoneProduction = 1.0f;
        f32 coverage = 1.0f;
        f32 loyalty = 1.0f;
        f32 recruitCost = 1.0f;
    };

    struct RaceInfo
    {
        std::string id;
        std::string name;
        std::string description;
        SpriteId sprite = SpriteId::Human;
        Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::vector<std::string> faiths;
        std::string defaultFaith;
        RaceModifiers modifiers;
        std::vector<std::string> preferredTerrain;
        f32 forestAffinity = 1.0f;
        f32 hillAffinity = 1.0f;
    };

    struct FaithInfo
    {
        std::string id;
        std::string name;
        Color color{ 1.0f, 1.0f, 1.0f, 1.0f };
        f32 loyaltyBonus = 0.0f;
        f32 moraleBonus = 0.0f;
    };

    class RaceDatabase final : public Singleton<RaceDatabase>
    {
        friend class Singleton<RaceDatabase>;
    public:
        void Load();

        const std::vector<RaceInfo>& Races() const { return m_races; }
        const RaceInfo& Race(const std::string& id) const;
        const std::vector<FaithInfo>& Faiths() const { return m_faiths; }
        const FaithInfo& Faith(const std::string& id) const;

        f32 ConversionBaseCost() const { return m_conversionBaseCost; }
        f32 ConversionCostPerPopulation() const { return m_conversionCostPerPopulation; }
        i32 ConversionDays() const { return m_conversionDays; }
        f32 ConversionLoyaltyShock() const { return m_conversionLoyaltyShock; }
        f32 ConversionSameRaceDiscount() const { return m_conversionSameRaceDiscount; }

    private:
        RaceDatabase() = default;
        ~RaceDatabase() = default;

        std::vector<RaceInfo> m_races;
        std::vector<FaithInfo> m_faiths;

        f32 m_conversionBaseCost = 420.0f;
        f32 m_conversionCostPerPopulation = 0.9f;
        i32 m_conversionDays = 120;
        f32 m_conversionLoyaltyShock = 0.25f;
        f32 m_conversionSameRaceDiscount = 0.7f;
    };
}
