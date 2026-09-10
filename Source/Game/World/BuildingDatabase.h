// BuildingDatabase.h - buildings.json in memory.
//
// Each entry names the decorator family that will wrap a settlement's calculations
// once the building stands; see Game/Decorators for the runtime side.
#pragma once

#include "../../Core/Singleton.h"
#include "ResourceData.h"

namespace woc
{
    enum class DecoratorKind : u8 { Production, Defense, Military, Loyalty, Coverage };

    struct BuildingRequirement
    {
        f32 forestNearby = 0.0f;
        f32 stoneNearby = 0.0f;
        f32 coastNearby = 0.0f;
        f32 waterNearby = 0.0f;
        std::string requiresBuilding;
    };

    struct BuildingInfo
    {
        std::string id;
        std::string name;
        std::string description;
        std::vector<std::string> allowedKinds;
        ResourceData cost;
        i32 buildDays = 30;
        DecoratorKind decorator = DecoratorKind::Production;
        BuildingRequirement requirement;

        // Production decorator
        ResourceType resource = ResourceType::Food;
        f32 multiplier = 1.0f;
        f32 flat = 0.0f;

        // Defence / military / loyalty / coverage decorators
        f32 defenseMultiplier = 1.0f;
        f32 trainingBonus = 0.0f;
        f32 recruitCostMultiplier = 1.0f;
        f32 cavalryCostMultiplier = 1.0f;
        f32 moraleBonus = 0.0f;
        f32 loyaltyPerMonth = 0.0f;
        f32 conversionCostMultiplier = 1.0f;
        f32 distancePenaltyMultiplier = 1.0f;
        f32 coverageMultiplier = 1.0f;
        f32 caravanBonus = 0.0f;

        // Extras
        f32 forestHarvest = 0.0f;
        f32 fieldRadiusBonus = 0.0f;
        f32 siegeSupplyDays = 0.0f;
        f32 bridgeRadius = 0.0f;
        bool bridgesWater = false;
        bool revealsQuarrySprite = false;

        bool AllowedFor(const std::string& kindId) const;
    };

    class BuildingDatabase final : public Singleton<BuildingDatabase>
    {
        friend class Singleton<BuildingDatabase>;
    public:
        void Load();

        const std::vector<BuildingInfo>& All() const { return m_buildings; }
        const BuildingInfo* Find(const std::string& id) const;

    private:
        BuildingDatabase() = default;
        ~BuildingDatabase() = default;

        std::vector<BuildingInfo> m_buildings;
    };
}
