// SettlementDatabase.h - settlements.json in memory: kinds, tiers and world actions.
#pragma once

#include "../../Core/Singleton.h"
#include "ResourceData.h"
#include "../../Render/RenderTypes.h"

namespace woc
{
    enum class SettlementKind : u8 { Village = 0, City, Castle, Count };

    struct SettlementTier
    {
        i32 tier = 1;
        std::string name;
        /// What this tier looks like on the map. Left unset in the config, the kind's own
        /// sprite stands for it, which is what the first tier of everything does.
        SpriteId sprite = SpriteId::Count;
        i32 minPopulation = 0;
        i32 maxPopulation = 1000;
        f32 coverage = 0.0f;
        f32 production = 1.0f;
        i32 garrison = 1;
        f32 prosperityCap = 1.0f;
    };

    struct SettlementKindInfo
    {
        SettlementKind kind = SettlementKind::Village;
        std::string id;
        std::string name;
        SpriteId sprite = SpriteId::Village;
        ResourceType baseResource = ResourceType::Food;
        bool producesResource = true;
        bool canBeIndependent = true;
        ResourceData buildCost;
        i32 buildDays = 60;
        /// How many people a newly founded seat of this kind gathers from the holdings
        /// around it. Nobody appears out of thin air: these are somebody else's subjects,
        /// packed up and sent out to the new site.
        i32 settlers = 200;
        /// How far the call for settlers carries, in map units.
        f32 settlerRange = 500.0f;
        std::vector<SettlementTier> tiers;

        /// Tier index (0-based) matching a population figure.
        size_t TierForPopulation(i32 population) const;
    };

    class SettlementDatabase final : public Singleton<SettlementDatabase>
    {
        friend class Singleton<SettlementDatabase>;
    public:
        void Load();

        const SettlementKindInfo& Kind(SettlementKind kind) const;
        const std::vector<SettlementKindInfo>& Kinds() const { return m_kinds; }
        static const char* KindId(SettlementKind kind);
        static SettlementKind ParseKind(const std::string& id);

        f32 StartingProsperity() const { return m_startingProsperity; }
        f32 StartingLoyalty() const { return m_startingLoyalty; }
        f32 DefenseBase() const { return m_defenseBase; }
        f32 SiegeSupplyDays() const { return m_siegeSupplyDays; }
        f32 MinDistanceBetween() const { return m_minDistance; }
        f32 MinDistanceVillage() const { return m_minDistanceVillage; }

        /// Raw action table (raid, raze, convert, ...) straight from the config.
        const Json& Action(const std::string& id) const;

    private:
        SettlementDatabase() = default;
        ~SettlementDatabase() = default;

        std::vector<SettlementKindInfo> m_kinds;
        Json m_actions;

        f32 m_startingProsperity = 0.5f;
        f32 m_startingLoyalty = 0.75f;
        f32 m_defenseBase = 1.0f;
        f32 m_siegeSupplyDays = 90.0f;
        f32 m_minDistance = 70.0f;
        f32 m_minDistanceVillage = 46.0f;
    };
}
