#include "CoverageSystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../Map/Pathfinder.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"
#include "../../Render/Renderer.h"

#include <algorithm>
#include <limits>

namespace woc
{
    void CoverageSystem::AssignPaletteSlots(World& world)
    {
        // Slot 0 is reserved for "unclaimed"; the renderer treats it as transparent.
        m_slotToClan.assign(1, kInvalidId);

        std::vector<EntityId> clanIds;
        clanIds.reserve(world.Clans().size());
        for (const auto& [id, clan] : world.Clans())
        {
            if (!clan.eliminated) clanIds.push_back(id);
        }
        std::sort(clanIds.begin(), clanIds.end());

        std::vector<Color> palette;
        palette.push_back(Color(0.0f, 0.0f, 0.0f, 0.0f));

        for (EntityId clanId : clanIds)
        {
            if (m_slotToClan.size() >= 32) break;   // the shader palette holds 32 entries
            Clan* clan = world.FindClan(clanId);
            if (!clan) continue;
            clan->paletteSlot = static_cast<u8>(m_slotToClan.size());
            m_slotToClan.push_back(clanId);
            palette.push_back(clan->color);
        }

        Renderer::Get().SetOwnerPalette(palette);
        m_tilesPerSlot.assign(m_slotToClan.size(), 0);
    }

    void CoverageSystem::Recompute(World& world)
    {
        MapData& map = world.MutableMap();
        if (!map.IsValid()) return;

        AssignPaletteSlots(world);

        ConfigManager& config = ConfigManager::Get();
        const f32 costPerUnit = config.Float("coverage/costPerUnit", 26.0f);
        const u32 maxNodes = static_cast<u32>(config.Int("coverage/maxNodesPerSettlement", 90000));
        const f32 coreCost = config.Float("coverage/coreCost", 12.0f);

        const size_t tileCount = map.Tiles().size();
        const f32 infinity = std::numeric_limits<f32>::max();
        m_bestScore.assign(tileCount, infinity);
        m_scratchOwner.assign(tileCount, kInvalidId);
        map.ClearOwners();

        std::vector<f32> floodCost;
        const TileCostFn coverageCost = &Pathfinder::CoverageCost;

        for (const auto& [id, settlement] : world.Settlements())
        {
            if (settlement.owner == kInvalidId) continue;

            // Only cities and castles project coverage; villages merely sit inside it.
            Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(settlement, map);
            const f32 strength = evaluator->Coverage();
            if (strength <= 0.001f) continue;

            const f32 budget = strength * costPerUnit;
            const Coord origin = map.ToTile(settlement.position);
            Pathfinder::Get().FloodFill(map, origin, budget, coverageCost, floodCost, maxNodes);

            for (size_t tile = 0; tile < tileCount; ++tile)
            {
                const f32 cost = floodCost[tile];
                if (cost >= infinity) continue;

                // Normalise so a stronger settlement wins contested ground - except for the
                // ground immediately around a seat, which its holder always keeps. Without
                // that, taking a castle inside a rival's reach would win almost no land.
                f32 score = cost / budget;
                if (cost <= coreCost) score *= 0.12f;
                if (score < m_bestScore[tile])
                {
                    m_bestScore[tile] = score;
                    m_scratchOwner[tile] = settlement.id;
                }
            }
        }

        // Turn the winning settlement per tile into a clan palette slot.
        std::vector<Tile>& tiles = map.Tiles();
        for (size_t tile = 0; tile < tileCount; ++tile)
        {
            const EntityId settlementId = m_scratchOwner[tile];
            if (settlementId == kInvalidId) continue;

            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;
            const Clan* clan = world.FindClan(settlement->owner);
            if (!clan) continue;

            tiles[tile].owner = clan->paletteSlot;
            if (clan->paletteSlot < m_tilesPerSlot.size()) ++m_tilesPerSlot[clan->paletteSlot];
        }

        // Cache each settlement's effective coverage for the info panels.
        for (auto& [id, settlement] : world.Settlements())
        {
            Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(settlement, map);
            settlement.coverageStrength = evaluator->Coverage();
        }

        // Ownership of open countryside is exactly coverage: a village outside every
        // castle's and city's reach answers to nobody, and one inside answers to whoever
        // reaches it. Both directions are resolved here, in that order.
        ReleaseUncoveredVillages(world);
        AbsorbIndependentVillages(world);

        Renderer::Get().SetOwnerMask(map.BuildOwnerMask(), map.TileWidth(), map.TileHeight());
        m_dirty = false;
    }

    void CoverageSystem::ReleaseUncoveredVillages(World& world)
    {
        MapData& map = world.MutableMap();

        std::vector<EntityId> released;
        for (const auto& [id, settlement] : world.Settlements())
        {
            if (settlement.IsIndependent()) continue;
            if (settlement.kind != SettlementKind::Village) continue;   // towns hold themselves

            const Clan* owner = world.FindClan(settlement.owner);
            if (!owner) continue;

            const u8 slot = map.At(map.ToTile(settlement.position)).owner;
            if (slot == owner->paletteSlot) continue;

            // A garrison holds a village that coverage no longer reaches.
            bool garrisoned = false;
            for (const auto& [cohortId, cohort] : world.Cohorts())
            {
                if (cohort.garrisonOf == id && cohort.clan == settlement.owner) { garrisoned = true; break; }
            }
            if (garrisoned) continue;

            released.push_back(id);
        }

        for (EntityId id : released)
        {
            Settlement* village = world.FindSettlement(id);
            if (!village) continue;
            Clan* owner = world.FindClan(village->owner);
            if (owner) owner->RemoveSettlement(id);

            village->owner = kInvalidId;
            village->loyalty = std::max(village->loyalty, 0.55f);
            world.Log(village->name + " більше не тримається жодним володарем",
                      Color::FromRGB(0x8B97A4));
        }
    }

    void CoverageSystem::AbsorbIndependentVillages(World& world)
    {
        MapData& map = world.MutableMap();
        std::vector<std::pair<EntityId, EntityId>> claims;   // village -> clan

        for (const auto& [id, settlement] : world.Settlements())
        {
            if (!settlement.IsIndependent()) continue;
            if (settlement.kind != SettlementKind::Village) continue;
            // A village that has just revolted must be retaken by force, not by paperwork.
            if (world.Time().TotalDays() < settlement.rebelliousUntilDay) continue;

            const Coord tile = map.ToTile(settlement.position);
            const u8 slot = map.At(tile).owner;
            if (slot == 0 || slot >= m_slotToClan.size()) continue;
            claims.emplace_back(id, m_slotToClan[slot]);
        }

        for (const auto& [villageId, clanId] : claims)
        {
            Settlement* village = world.FindSettlement(villageId);
            Clan* clan = world.FindClan(clanId);
            if (!village || !clan) continue;

            village->owner = clanId;
            clan->AddSettlement(villageId);

            // Being absorbed is not the same as being welcomed.
            ConfigManager& config = ConfigManager::Get();
            f32 loyalty = 0.72f;
            if (village->raceId != clan->raceId)
                loyalty -= config.Float("population/loyaltyForeignRacePenalty", 0.18f);
            if (village->faithId != clan->faithId)
                loyalty -= config.Float("population/loyaltyForeignFaithPenalty", 0.12f);
            village->loyalty = std::clamp(loyalty, 0.1f, 1.0f);

            world.Log(village->name + " визнає владу роду " + clan->name, clan->color);
        }
    }

    EntityId CoverageSystem::OwnerAt(const World& world, const Vec2& position) const
    {
        const MapData& map = world.Map();
        if (!map.IsValid()) return kInvalidId;

        const u8 slot = map.At(map.ToTile(position)).owner;
        if (slot == 0 || slot >= m_slotToClan.size()) return kInvalidId;
        return m_slotToClan[slot];
    }

    u32 CoverageSystem::TilesOwnedBy(EntityId clanId) const
    {
        for (size_t slot = 0; slot < m_slotToClan.size(); ++slot)
        {
            if (m_slotToClan[slot] == clanId)
            {
                return slot < m_tilesPerSlot.size() ? m_tilesPerSlot[slot] : 0u;
            }
        }
        return 0;
    }
}
