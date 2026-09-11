#include "CoverageSystem.h"
#include "FogSystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../Map/Pathfinder.h"
#include "../World/RaceDatabase.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/JobSystem.h"
#include "../../Core/Profiler.h"
#include "../../Core/Log.h"
#include "../../Render/Renderer.h"

#include <algorithm>
#include <limits>
#include <queue>

namespace woc
{
    namespace
    {
        constexpr f32 kDiagonalStep = 1.41421356f;
        constexpr u8 kMaxSlots = 127;     // the shader palette holds 128, slot 0 is "nothing"

        constexpr i32 kNeighbours[8][2] = {
            { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
            { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 }
        };

        /// A label in the flood: the score is normalised by the seed's own budget, which is
        /// what lets several seats share one queue and still contest ground fairly.
        struct Label
        {
            f32 score = 0.0f;
            f32 raw = 0.0f;
            u32 tile = 0;
            u32 seed = 0;

            bool operator>(const Label& other) const { return score > other.score; }
        };

        /// A seat's own shade inside its clan's colour, so neighbouring zones of influence
        /// read apart without the map turning into a harlequin.
        Color ShadeFor(const Color& base, size_t index)
        {
            static const f32 kSteps[6] = { 1.0f, 0.76f, 1.24f, 0.88f, 1.12f, 0.64f };
            const f32 factor = kSteps[index % 6];
            return { std::min(1.0f, base.r * factor),
                     std::min(1.0f, base.g * factor),
                     std::min(1.0f, base.b * factor), base.a };
        }
    }

    CoverageSystem::~CoverageSystem()
    {
        // Never leave the worker running past the end of the process.
        if (m_pending.valid()) m_pending.wait();
    }

    const char* CoverageSystem::ModeName(MapMode mode)
    {
        switch (mode)
        {
        case MapMode::Realms:    return "Держави";
        case MapMode::Influence: return "Зони впливу";
        case MapMode::Races:     return "Народи";
        case MapMode::Faiths:    return "Віри";
        }
        return "?";
    }

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

        for (EntityId clanId : clanIds)
        {
            if (m_slotToClan.size() > kMaxSlots) break;
            Clan* clan = world.FindClan(clanId);
            if (!clan) continue;
            clan->paletteSlot = static_cast<u8>(m_slotToClan.size());
            m_slotToClan.push_back(clanId);
        }

        m_tilesPerSlot.assign(m_slotToClan.size(), 0);
    }

    // =========================================================================================
    // The job: built on the main thread, run anywhere, applied on the main thread
    // =========================================================================================

    CoverageSystem::Job CoverageSystem::BuildJob(World& world)
    {
        MapData& map = world.MutableMap();
        ConfigManager& config = ConfigManager::Get();

        // The last pass's buffers come back here to be refilled: four arrays of one entry
        // per tile is half a megabyte, and there is no reason to ask the allocator for it
        // again every time the borders move.
        Job job = std::move(m_scratch);
        job.seeds.clear();
        job.mayEnter.clear();
        job.width = map.TileWidth();
        job.height = map.TileHeight();
        job.coreCost = config.Float("coverage/coreCost", 12.0f);
        job.clanSlots = static_cast<u8>(m_slotToClan.size());

        const size_t tileCount = map.Tiles().size();

        // Snapshot the terrain cost once. The flood then never touches the world again,
        // which is both faster (no std::function per neighbour) and safe off-thread.
        job.stepCost.resize(tileCount);
        job.heldBy.resize(tileCount);
        for (u32 y = 0; y < job.height; ++y)
        {
            for (u32 x = 0; x < job.width; ++x)
            {
                const Coord tile{ static_cast<i32>(x), static_cast<i32>(y) };
                const size_t index = map.Index(tile);
                job.stepCost[index] = map.CoverageCost(tile);
                job.heldBy[index] = map.At(tile).owner;
            }
        }

        const f32 costPerUnit = config.Float("coverage/costPerUnit", 26.0f);
        for (auto& [id, settlement] : world.Settlements())
        {
            // Cache the effective coverage for the info panels while the evaluator is here.
            Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(settlement, map);
            const f32 strength = evaluator->Coverage();
            settlement.coverageStrength = strength;

            // Only cities and castles project coverage; villages merely sit inside it.
            if (settlement.owner == kInvalidId || strength <= 0.001f) continue;

            const Clan* clan = world.FindClan(settlement.owner);
            if (!clan) continue;

            const Coord origin = map.ToTile(settlement.position);
            if (!map.InBounds(origin)) continue;

            job.seeds.push_back({ settlement.id, static_cast<u32>(map.Index(origin)),
                                  strength * costPerUnit, clan->paletteSlot });
        }

        // Who may take ground from whom. A clan always keeps its own; anyone may move into
        // no man's land; taking a rival's country means being at war with him. A slot whose
        // clan no longer projects any coverage holds nothing, so its old land is free.
        std::vector<u8> stillHolds(job.clanSlots, 0);
        for (const Seed& seed : job.seeds) stillHolds[seed.clanSlot] = 1;

        job.mayEnter.assign(static_cast<size_t>(job.clanSlots) * job.clanSlots, 1);
        for (u8 mine = 1; mine < job.clanSlots; ++mine)
        {
            for (u8 theirs = 1; theirs < job.clanSlots; ++theirs)
            {
                if (mine == theirs) continue;
                const bool free = !stillHolds[theirs] ||
                                  world.AreHostile(m_slotToClan[mine], m_slotToClan[theirs]);
                job.mayEnter[static_cast<size_t>(mine) * job.clanSlots + theirs] = free ? 1 : 0;
            }
        }

        const u32 perSeed = static_cast<u32>(config.Int("coverage/maxNodesPerSettlement", 90000));
        job.maxNodes = static_cast<u32>(std::min<u64>(
            static_cast<u64>(perSeed) * std::max<size_t>(1, job.seeds.size()),
            static_cast<u64>(tileCount) * 3));

        return job;
    }

    void CoverageSystem::RunJob(Job& job)
    {
        const size_t tileCount = job.stepCost.size();
        const f32 infinity = std::numeric_limits<f32>::max();

        job.owner.assign(tileCount, kInvalidId);
        if (job.seeds.empty() || tileCount == 0) return;

        std::vector<f32> bestScore(tileCount, infinity);

        auto allowed = [&job](u8 seedSlot, u32 tile)
        {
            const u8 held = job.heldBy[tile];
            if (held == 0 || held == seedSlot) return true;
            if (held >= job.clanSlots || seedSlot >= job.clanSlots) return true;
            return job.mayEnter[static_cast<size_t>(seedSlot) * job.clanSlots + held] != 0;
        };

        std::priority_queue<Label, std::vector<Label>, std::greater<Label>> open;
        for (u32 s = 0; s < job.seeds.size(); ++s)
        {
            const Seed& seed = job.seeds[s];
            if (seed.origin >= tileCount) continue;
            if (bestScore[seed.origin] == 0.0f) continue;   // another seat sits on this tile

            bestScore[seed.origin] = 0.0f;
            job.owner[seed.origin] = seed.settlement;
            open.push({ 0.0f, 0.0f, seed.origin, s });
        }

        const i32 width = static_cast<i32>(job.width);
        const i32 height = static_cast<i32>(job.height);
        u32 expanded = 0;

        while (!open.empty() && expanded < job.maxNodes)
        {
            const Label node = open.top();
            open.pop();
            if (node.score > bestScore[node.tile]) continue;   // a stale label
            ++expanded;

            const Seed& seed = job.seeds[node.seed];
            const i32 x = static_cast<i32>(node.tile % job.width);
            const i32 y = static_cast<i32>(node.tile / job.width);

            for (int i = 0; i < 8; ++i)
            {
                const i32 nx = x + kNeighbours[i][0];
                const i32 ny = y + kNeighbours[i][1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;

                const u32 neighbour = static_cast<u32>(ny) * job.width + static_cast<u32>(nx);
                const f32 step = job.stepCost[neighbour];
                if (step < 0.0f) continue;
                if (!allowed(seed.clanSlot, neighbour)) continue;   // a neighbour's land, at peace

                const bool diagonal = i >= 4;
                if (diagonal)
                {
                    // No slipping through the corner between two impassable tiles.
                    if (job.stepCost[static_cast<u32>(y) * job.width + static_cast<u32>(nx)] < 0.0f) continue;
                    if (job.stepCost[static_cast<u32>(ny) * job.width + static_cast<u32>(x)] < 0.0f) continue;
                }

                const f32 raw = node.raw + step * (diagonal ? kDiagonalStep : 1.0f);
                if (raw > seed.budget) continue;

                // Normalise so a stronger settlement wins contested ground - except for the
                // ground immediately around a seat, which its holder always keeps. Without
                // that, taking a castle inside a rival's reach would win almost no land.
                f32 score = raw / seed.budget;
                if (raw <= job.coreCost) score *= 0.12f;

                if (score >= bestScore[neighbour]) continue;

                bestScore[neighbour] = score;
                job.owner[neighbour] = seed.settlement;
                open.push({ score, raw, neighbour, node.seed });
            }
        }
    }

    void CoverageSystem::ApplyJob(World& world, const Job& job)
    {
        MapData& map = world.MutableMap();
        std::vector<Tile>& tiles = map.Tiles();
        if (job.owner.size() != tiles.size()) return;   // the map changed under the worker

        map.ClearOwners();
        std::fill(m_tilesPerSlot.begin(), m_tilesPerSlot.end(), 0u);

        // Every seat that won any ground gets an influence slot of its own; that is what
        // lets the influence map draw the seams inside a realm.
        m_slotToHolder.assign(1, kInvalidId);

        // Settlement -> (clan slot, influence slot), resolved once instead of per tile.
        std::vector<std::pair<EntityId, std::pair<u8, u8>>> slotOf;
        slotOf.reserve(world.Settlements().size());
        for (const auto& [id, settlement] : world.Settlements())
        {
            const Clan* clan = world.FindClan(settlement.owner);
            if (!clan) continue;

            u8 influence = 0;
            if (m_slotToHolder.size() <= kMaxSlots)
            {
                influence = static_cast<u8>(m_slotToHolder.size());
                m_slotToHolder.push_back(id);
            }
            slotOf.emplace_back(id, std::make_pair(clan->paletteSlot, influence));
        }
        std::sort(slotOf.begin(), slotOf.end());

        EntityId cachedId = kInvalidId;
        std::pair<u8, u8> cached{ 0, 0 };
        for (size_t tile = 0; tile < tiles.size(); ++tile)
        {
            const EntityId settlementId = job.owner[tile];
            if (settlementId == kInvalidId) continue;

            if (settlementId != cachedId)
            {
                const auto it = std::lower_bound(slotOf.begin(), slotOf.end(),
                                                 std::make_pair(settlementId, std::make_pair(u8{ 0 }, u8{ 0 })));
                if (it == slotOf.end() || it->first != settlementId) continue;
                cachedId = settlementId;
                cached = it->second;
            }

            tiles[tile].owner = cached.first;
            tiles[tile].holder = cached.second;
            if (cached.first < m_tilesPerSlot.size()) ++m_tilesPerSlot[cached.first];
        }

        // Coverage decides who *gains* open country: an unclaimed village inside a lord's
        // reach acknowledges him. It never takes anything away - a village already sworn to
        // a lord leaves him only by revolt or by conquest, never because a border moved.
        AbsorbIndependentVillages(world);

        RefreshLayer(world);
    }

    // =========================================================================================
    // The coloured layer
    // =========================================================================================

    void CoverageSystem::SetMode(MapMode mode, World& world)
    {
        if (m_mode == mode) return;
        m_mode = mode;
        RefreshLayer(world);
    }

    void CoverageSystem::RefreshLayer(World& world)
    {
        WOC_PROFILE("coverage.buildMask");
        MapData& map = world.MutableMap();
        if (!map.IsValid()) return;

        Renderer& renderer = Renderer::Get();
        const RaceDatabase& races = RaceDatabase::Get();
        const std::vector<Tile>& tiles = map.Tiles();

        std::vector<Color> palette;
        palette.push_back(Color(0.0f, 0.0f, 0.0f, 0.0f));   // slot 0 draws nothing

        std::vector<u8> mask(tiles.size(), 0);

        switch (m_mode)
        {
        case MapMode::Realms:
        {
            for (size_t slot = 1; slot < m_slotToClan.size(); ++slot)
            {
                const Clan* clan = world.FindClan(m_slotToClan[slot]);
                palette.push_back(clan ? clan->color : Color(0.5f, 0.5f, 0.5f, 0.0f));
            }
            for (size_t i = 0; i < tiles.size(); ++i) mask[i] = tiles[i].owner;
            renderer.SetBorderStrength(0.92f);
            renderer.SetOwnerTint(renderer.DefaultOwnerTint());
            break;
        }
        case MapMode::Influence:
        {
            // One shade per seat: the outer frontier still stands out because neighbouring
            // realms use different hues, and now the seams inside a realm are visible too.
            size_t shadeIndex = 0;
            for (size_t slot = 1; slot < m_slotToHolder.size(); ++slot)
            {
                const Settlement* settlement = world.FindSettlement(m_slotToHolder[slot]);
                const Clan* clan = settlement ? world.FindClan(settlement->owner) : nullptr;
                palette.push_back(clan ? ShadeFor(clan->color, shadeIndex++)
                                       : Color(0.5f, 0.5f, 0.5f, 0.0f));
            }
            for (size_t i = 0; i < tiles.size(); ++i) mask[i] = tiles[i].holder;
            renderer.SetBorderStrength(0.55f);
            renderer.SetOwnerTint(renderer.DefaultOwnerTint());
            break;
        }
        case MapMode::Races:
        case MapMode::Faiths:
        {
            // Slots are the peoples or the gods themselves; every tile takes the one its
            // holding seat belongs to, so the map answers "who lives here", not "who rules".
            const size_t count = m_mode == MapMode::Races ? races.Races().size()
                                                          : races.Faiths().size();
            for (size_t i = 0; i < count && palette.size() <= kMaxSlots; ++i)
            {
                palette.push_back(m_mode == MapMode::Races ? races.Races()[i].color
                                                           : races.Faiths()[i].color);
            }

            // settlement influence slot -> the slot of its people or its faith
            std::vector<u8> remap(m_slotToHolder.size(), 0);
            for (size_t slot = 1; slot < m_slotToHolder.size(); ++slot)
            {
                const Settlement* settlement = world.FindSettlement(m_slotToHolder[slot]);
                if (!settlement) continue;

                if (m_mode == MapMode::Races)
                {
                    for (size_t i = 0; i < races.Races().size(); ++i)
                    {
                        if (races.Races()[i].id == settlement->raceId) { remap[slot] = static_cast<u8>(i + 1); break; }
                    }
                }
                else
                {
                    for (size_t i = 0; i < races.Faiths().size(); ++i)
                    {
                        if (races.Faiths()[i].id == settlement->faithId) { remap[slot] = static_cast<u8>(i + 1); break; }
                    }
                }
            }

            for (size_t i = 0; i < tiles.size(); ++i)
            {
                const u8 holder = tiles[i].holder;
                mask[i] = holder < remap.size() ? remap[holder] : 0;
            }
            renderer.SetBorderStrength(0.75f);
            renderer.SetOwnerTint(ConfigManager::Get().Float("render/thematicTint", 0.62f));
            break;
        }
        }

        renderer.SetOwnerPalette(palette);
        m_lastPalette = palette;

        // Live borders only where somebody is watching. Everything else is what the player
        // remembers, which is the whole point of drawing a border at all.
        FogSystem::Get().FilterOwnerMask(mask);

        // Every upload costs a device sync, and the layer is unchanged on most ticks.
        if (!m_lastMask.empty() && mask == m_lastMask) return;
        m_lastMask = mask;
        ++m_layerRevision;
        renderer.SetOwnerMask(mask, map.TileWidth(), map.TileHeight());
    }

    // =========================================================================================
    // Entry points
    // =========================================================================================

    void CoverageSystem::Recompute(World& world)
    {
        if (!world.Map().IsValid()) return;
        if (m_busy) return;             // a pass is already in flight; it will pick this up

        { WOC_PROFILE("coverage.slots"); AssignPaletteSlots(world); }
        Job job;
        { WOC_PROFILE("coverage.buildJob"); job = BuildJob(world); }

        m_pending = JobSystem::Get().Run([work = std::move(job)]() mutable
        {
            RunJob(work);
            return std::move(work);
        });

        m_busy = true;
        m_dirty = false;
    }

    void CoverageSystem::RecomputeBlocking(World& world)
    {
        if (!world.Map().IsValid()) return;

        // Let any in-flight pass finish first, then throw its result away: it was started
        // from an older world and this caller wants the borders as they are now.
        if (m_pending.valid()) m_pending.get();
        m_busy = false;

        AssignPaletteSlots(world);
        Job job = BuildJob(world);
        RunJob(job);
        ApplyJob(world, job);
        m_dirty = false;
    }

    void CoverageSystem::Update(World& world)
    {
        if (!m_busy || !m_pending.valid()) return;
        if (m_pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

        Job job = m_pending.get();
        m_busy = false;
        ApplyJob(world, job);
        m_scratch = std::move(job);   // its vectors are the next pass's, capacity and all
    }

    // =========================================================================================
    // Villages follow the border
    // =========================================================================================

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

    EntityId CoverageSystem::HolderAt(const World& world, const Vec2& position) const
    {
        const MapData& map = world.Map();
        if (!map.IsValid()) return kInvalidId;

        const u8 slot = map.At(map.ToTile(position)).holder;
        if (slot == 0 || slot >= m_slotToHolder.size()) return kInvalidId;
        return m_slotToHolder[slot];
    }

    std::vector<EntityId> CoverageSystem::SubjectsOf(const World& world, EntityId settlementId) const
    {
        std::vector<EntityId> subjects;
        for (const auto& [id, settlement] : world.Settlements())
        {
            if (id == settlementId) continue;
            if (HolderAt(world, settlement.position) == settlementId) subjects.push_back(id);
        }
        return subjects;
    }

    bool CoverageSystem::IsCovered(const World& world, const Vec2& position) const
    {
        const MapData& map = world.Map();
        if (!map.IsValid()) return false;
        return map.At(map.ToTile(position)).owner != 0;
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
