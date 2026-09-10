#include "SettlementSystem.h"
#include "CoverageSystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../Factories/SettlementFactory.h"
#include "../Factories/UnitFactory.h"
#include "../World/RaceDatabase.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Random.h"

#include <algorithm>

namespace woc
{
    bool SettlementSystem::RequirementsMet(const World& world, const Settlement& settlement,
                                           const BuildingInfo& building, std::string& reason) const
    {
        if (!building.AllowedFor(SettlementDatabase::KindId(settlement.kind)))
        {
            reason = "Не будується в цьому типі поселення";
            return false;
        }
        if (settlement.HasBuilding(building.id)) { reason = "Уже збудовано"; return false; }
        if (settlement.IsBuilding(building.id)) { reason = "Уже будується"; return false; }

        if (!building.requirement.requiresBuilding.empty() &&
            !settlement.HasBuilding(building.requirement.requiresBuilding))
        {
            const BuildingInfo* prerequisite = BuildingDatabase::Get().Find(building.requirement.requiresBuilding);
            reason = "Потрібно: " + (prerequisite ? prerequisite->name : building.requirement.requiresBuilding);
            return false;
        }

        const MapData& map = world.Map();
        const f32 radius = 110.0f;

        if (building.requirement.forestNearby > 0.0f &&
            map.SampleForest(settlement.position, radius) < building.requirement.forestNearby)
        {
            reason = "Поблизу немає лісу";
            return false;
        }
        if (building.requirement.stoneNearby > 0.0f &&
            map.SampleStone(settlement.position, radius) < building.requirement.stoneNearby)
        {
            reason = "Поблизу немає каменю";
            return false;
        }
        if (building.requirement.coastNearby > 0.0f &&
            map.SampleCoast(settlement.position, radius) < building.requirement.coastNearby)
        {
            reason = "Немає виходу до моря";
            return false;
        }
        if (building.requirement.waterNearby > 0.0f &&
            map.SampleWater(settlement.position, radius) < building.requirement.waterNearby)
        {
            reason = "Поблизу немає води";
            return false;
        }
        return true;
    }

    std::vector<BuildOption> SettlementSystem::BuildOptions(World& world, EntityId settlementId) const
    {
        std::vector<BuildOption> options;
        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return options;

        const Clan* clan = world.FindClan(settlement->owner);

        for (const BuildingInfo& building : BuildingDatabase::Get().All())
        {
            BuildOption option;
            option.building = &building;
            option.allowed = RequirementsMet(world, *settlement, building, option.blockedReason);
            option.affordable = clan && clan->resources.CanAfford(building.cost);
            if (option.allowed && !option.affordable) option.blockedReason = "Бракує ресурсів";
            options.push_back(option);
        }
        return options;
    }

    std::vector<RecruitOption> SettlementSystem::RecruitOptions(World& world, EntityId settlementId) const
    {
        std::vector<RecruitOption> options;
        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return options;

        const Clan* clan = world.FindClan(settlement->owner);
        const UnitDatabase& db = UnitDatabase::Get();
        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(*settlement, world.Map());

        const u32 establishment = UnitFactory::EstablishmentFor(*settlement);

        for (const RoleInfo& role : db.Roles())
        {
            const UnitData& stats = db.Stats(settlement->raceId, role.role);

            RecruitOption option;
            option.role = role.role;
            option.name = stats.name;
            option.headCount = role.single ? 1u : establishment;
            option.cost = stats.recruitCost * evaluator->RecruitCostMultiplier(role.role) *
                          (role.single ? 1.0f : static_cast<f32>(option.headCount) / 50.0f);

            if (!clan) option.blockedReason = "Поселення незалежне";
            else if (settlement->loyalty < db.MinLoyaltyToRecruit()) option.blockedReason = "Надто низька вірність";
            else if (settlement->population < static_cast<i32>(option.headCount) * 4) option.blockedReason = "Замало людей";
            else if (clan->resources.money < option.cost) option.blockedReason = "Бракує срібла";
            else option.affordable = true;

            options.push_back(option);
        }
        return options;
    }

    bool SettlementSystem::StartConstruction(World& world, EntityId settlementId, const std::string& buildingId)
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        const BuildingInfo* building = BuildingDatabase::Get().Find(buildingId);
        if (!settlement || !building) return false;

        Clan* clan = world.FindClan(settlement->owner);
        if (!clan) return false;

        std::string reason;
        if (!RequirementsMet(world, *settlement, *building, reason)) return false;
        if (!clan->resources.CanAfford(building->cost)) return false;

        clan->resources -= building->cost;
        settlement->construction.push_back({ buildingId, building->buildDays });
        world.Log(settlement->name + ": розпочато будівництво (" + building->name + ")", clan->color);
        return true;
    }

    bool SettlementSystem::CancelConstruction(World& world, EntityId settlementId, const std::string& buildingId)
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return false;

        const auto it = std::find_if(settlement->construction.begin(), settlement->construction.end(),
            [&buildingId](const ConstructionOrder& order) { return order.buildingId == buildingId; });
        if (it == settlement->construction.end()) return false;

        // Half the materials are recovered from an abandoned site.
        if (const BuildingInfo* building = BuildingDatabase::Get().Find(buildingId))
        {
            if (Clan* clan = world.FindClan(settlement->owner))
            {
                clan->resources += building->cost * 0.5f;
            }
        }
        settlement->construction.erase(it);
        return true;
    }

    EntityId SettlementSystem::Recruit(World& world, EntityId settlementId, EntityId cohortId, UnitRole role)
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return kInvalidId;

        Clan* clan = world.FindClan(settlement->owner);
        if (!clan) return kInvalidId;

        const std::vector<RecruitOption> options = RecruitOptions(world, settlementId);
        const auto it = std::find_if(options.begin(), options.end(),
            [role](const RecruitOption& option) { return option.role == role; });
        if (it == options.end() || !it->affordable) return kInvalidId;

        Random& random = GlobalRandom();

        Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort)
        {
            cohort = &UnitFactory::CreateCohort(world, clan->id, settlement->position, random);
            cohort->garrisonOf = settlement->id;
            cohort->currentTask.type = TaskType::Garrison;
            cohort->currentTask.targetSettlement = settlement->id;
        }
        if (cohort->units.size() >= UnitDatabase::Get().MaxUnitsPerCohort()) return kInvalidId;

        clan->resources.money -= it->cost;
        settlement->population = std::max(20, settlement->population - static_cast<i32>(it->headCount));
        settlement->loyalty = std::max(0.0f, settlement->loyalty -
            UnitDatabase::Get().LoyaltyCostPerUnit());

        Unit& unit = UnitFactory::Create(world, cohort->id, settlement->raceId, role,
                                         it->headCount, settlement->name, random);

        // Barracks and stables make better soldiers, not just cheaper ones.
        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(*settlement, world.Map());
        unit.training = Clamp01(unit.training + evaluator->TrainingBonus());
        unit.morale = Clamp01(unit.morale + evaluator->MoraleBonus());

        world.Log(settlement->name + ": набрано загін (" + unit.DisplayName() + ")", clan->color);
        return cohort->id;
    }

    f32 SettlementSystem::ConversionCost(World& world, EntityId settlementId, const std::string& faithId) const
    {
        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement || settlement->faithId == faithId) return 0.0f;

        const RaceDatabase& races = RaceDatabase::Get();
        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(*settlement, world.Map());

        f32 cost = races.ConversionBaseCost() +
                   static_cast<f32>(settlement->population) * races.ConversionCostPerPopulation();
        cost *= evaluator->ConversionCostMultiplier();

        // Converting your own people is cheaper than converting a conquered nation.
        const Clan* clan = world.FindClan(settlement->owner);
        if (clan && clan->raceId == settlement->raceId) cost *= races.ConversionSameRaceDiscount();
        return cost;
    }

    bool SettlementSystem::StartConversion(World& world, EntityId settlementId, const std::string& faithId)
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement || settlement->faithId == faithId) return false;
        if (settlement->conversionDaysLeft > 0) return false;

        Clan* clan = world.FindClan(settlement->owner);
        if (!clan) return false;

        const f32 cost = ConversionCost(world, settlementId, faithId);
        if (clan->resources.money < cost) return false;

        const RaceDatabase& races = RaceDatabase::Get();
        clan->resources.money -= cost;
        settlement->conversionTarget = faithId;
        settlement->conversionDaysLeft = races.ConversionDays();
        settlement->loyalty = std::max(0.0f, settlement->loyalty - races.ConversionLoyaltyShock());

        world.Log(settlement->name + ": розпочато навернення до " + races.Faith(faithId).name, clan->color);
        return true;
    }

    bool SettlementSystem::Raze(World& world, EntityId settlementId, EntityId actingClan)
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return false;
        if (settlement->kind != SettlementKind::Village) return false;   // only villages can be burnt out

        const Json& action = SettlementDatabase::Get().Action("raze");
        if (Clan* clan = world.FindClan(actingClan))
        {
            clan->resources.money += static_cast<f32>(settlement->population) *
                                     action["lootMoneyPerPopulation"].AsFloat(0.2f);
            world.Log(settlement->name + " спалено дощенту", Color::FromRGB(0xC05046));
        }

        world.DestroySettlement(settlementId);
        CoverageSystem::Get().MarkDirty();
        return true;
    }

    bool SettlementSystem::GrantIndependence(World& world, EntityId settlementId)
    {
        Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement || settlement->IsIndependent()) return false;

        if (Clan* clan = world.FindClan(settlement->owner)) clan->RemoveSettlement(settlementId);
        settlement->owner = kInvalidId;
        settlement->loyalty = SettlementDatabase::Get().Action("grantIndependence")["loyaltyRestored"].AsFloat(0.9f);
        world.Log(settlement->name + " відпущено на волю", Color::FromRGB(0xD2933A));
        CoverageSystem::Get().MarkDirty();
        return true;
    }

    EntityId SettlementSystem::Found(World& world, EntityId clanId, SettlementKind kind, const Vec2& position)
    {
        Clan* clan = world.FindClan(clanId);
        if (!clan) return kInvalidId;

        const SettlementKindInfo& info = SettlementDatabase::Get().Kind(kind);
        if (!clan->resources.CanAfford(info.buildCost)) return kInvalidId;
        if (!SettlementFactory::CanPlace(world, world.Map(), kind, position)) return kInvalidId;

        clan->resources -= info.buildCost;

        SettlementRequest request;
        request.kind = kind;
        request.position = position;
        request.raceId = clan->raceId;
        request.faithId = clan->faithId;
        request.owner = clanId;

        Settlement& settlement = SettlementFactory::Create(world, request, GlobalRandom());
        world.Log("Засновано поселення " + settlement.name, clan->color);
        CoverageSystem::Get().MarkDirty();
        return settlement.id;
    }

    void SettlementSystem::Tick(World& world, i32 days)
    {
        if (days <= 0) return;

        const RaceDatabase& races = RaceDatabase::Get();

        for (auto& [id, settlement] : world.Settlements())
        {
            // --- construction ---------------------------------------------------------------
            for (auto it = settlement.construction.begin(); it != settlement.construction.end();)
            {
                it->daysRemaining -= days;
                if (it->daysRemaining > 0) { ++it; continue; }

                settlement.buildings.push_back(it->buildingId);
                const BuildingInfo* building = BuildingDatabase::Get().Find(it->buildingId);
                if (building)
                {
                    if (building->revealsQuarrySprite) settlement.quarryRevealed = true;
                    if (building->coverageMultiplier != 1.0f) CoverageSystem::Get().MarkDirty();

                    // A bridge opens wide water to both troops and authority.
                    if (building->bridgesWater && building->bridgeRadius > 0.0f)
                    {
                        MapData& map = world.MutableMap();
                        const Coord center = map.ToTile(settlement.position);
                        const i32 span = std::max(1, static_cast<i32>(building->bridgeRadius / map.TilePixels()));
                        for (i32 dy = -span; dy <= span; ++dy)
                        {
                            for (i32 dx = -span; dx <= span; ++dx)
                            {
                                if (dx * dx + dy * dy > span * span) continue;
                                const Coord probe{ center.x + dx, center.y + dy };
                                if (!map.InBounds(probe)) continue;
                                if (TerrainDatabase::Get().At(map.At(probe).terrain).water)
                                {
                                    map.At(probe).bridged = true;
                                }
                            }
                        }
                        CoverageSystem::Get().MarkDirty();
                    }

                    if (const Clan* clan = world.FindClan(settlement.owner))
                    {
                        world.Log(settlement.name + ": збудовано — " + building->name, clan->color);
                    }
                }
                it = settlement.construction.erase(it);
            }

            // --- religious conversion ---------------------------------------------------------
            if (settlement.conversionDaysLeft > 0)
            {
                settlement.conversionDaysLeft -= days;
                if (settlement.conversionDaysLeft <= 0)
                {
                    settlement.faithId = settlement.conversionTarget;
                    settlement.conversionTarget.clear();
                    settlement.conversionDaysLeft = 0;
                    world.Log(settlement.name + " прийняв " + races.Faith(settlement.faithId).name,
                              Color::FromRGB(0xC9A227));
                }
            }
        }
    }
}
