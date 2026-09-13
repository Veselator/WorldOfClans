#include "FogSystem.h"
#include "CoverageSystem.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Profiler.h"
#include "../../Core/Json.h"
#include "../../Render/Renderer.h"

#include <algorithm>

namespace woc
{
    void FogSystem::Reset(World& world)
    {
        const MapData& map = world.Map();
        m_width = map.TileWidth();
        m_height = map.TileHeight();

        m_state.assign(static_cast<size_t>(m_width) * m_height, static_cast<u8>(FogState::Unseen));
        m_owners.assign(m_state.size(), 0);
        m_seen.clear();
        m_lastMask.clear();
        m_exploredMin = { 0, 0 };
        m_exploredMax = { -1, -1 };
        m_timer = 0.0f;
        m_dirty = true;
        ++m_revision;
    }

    // =========================================================================================
    // Looking around
    // =========================================================================================

    void FogSystem::Reveal(const World& world, const Vec2& centre, f32 radius)
    {
        const MapData& map = world.Map();
        const Coord origin = map.ToTile(centre);
        const i32 span = std::max(1, static_cast<i32>(radius / static_cast<f32>(map.TilePixels())));
        const i32 spanSq = span * span;

        for (i32 dy = -span; dy <= span; ++dy)
        {
            for (i32 dx = -span; dx <= span; ++dx)
            {
                if (dx * dx + dy * dy > spanSq) continue;

                const Coord tile{ origin.x + dx, origin.y + dy };
                if (!map.InBounds(tile)) continue;

                const size_t index = map.Index(tile);
                if (m_state[index] == static_cast<u8>(FogState::Unseen))
                {
                    // First sight of this tile: the known world has just grown.
                    if (m_exploredMax.x < m_exploredMin.x)
                    {
                        m_exploredMin = tile;
                        m_exploredMax = tile;
                    }
                    else
                    {
                        m_exploredMin = { std::min(m_exploredMin.x, tile.x), std::min(m_exploredMin.y, tile.y) };
                        m_exploredMax = { std::max(m_exploredMax.x, tile.x), std::max(m_exploredMax.y, tile.y) };
                    }
                    ++m_revision;
                }
                m_state[index] = static_cast<u8>(FogState::Visible);

                // Seeing a tile is also the moment its border is committed to memory.
                const u8 owner = map.At(tile).owner;
                if (m_owners[index] != owner)
                {
                    m_owners[index] = owner;
                    m_dirty = true;
                }
            }
        }
    }

    void FogSystem::Update(World& world, f32 realSeconds)
    {
        if (!m_enabled) return;

        const MapData& map = world.Map();
        if (!map.IsValid()) return;
        if (m_state.size() != map.Tiles().size()) Reset(world);

        // Recomputing every frame would be waste: an army covers a fraction of a tile in
        // that time. A few times a second is more than the eye can tell apart.
        ConfigManager& config = ConfigManager::Get();
        m_timer -= realSeconds;
        if (m_timer > 0.0f) return;
        m_timer = config.Float("fog/refreshSeconds", 0.2f);

        const State* state = world.HumanState();
        if (!state) return;

        // Last frame's visible ground drops back to merely explored; whatever is still
        // watched is raised again below.
        m_previousState = m_state;
        for (u8& value : m_state)
        {
            if (value == static_cast<u8>(FogState::Visible)) value = static_cast<u8>(FogState::Explored);
        }

        const f32 settlementSight = config.Float("fog/settlementSight", 190.0f);
        const f32 castleBonus = config.Float("fog/castleSightBonus", 90.0f);
        const f32 cohortSight = config.Float("fog/cohortSight", 150.0f);

        for (EntityId clanId : state->clans)
        {
            const Clan* clan = world.FindClan(clanId);
            if (!clan) continue;

            for (EntityId settlementId : clan->settlements)
            {
                const Settlement* settlement = world.FindSettlement(settlementId);
                if (!settlement) continue;

                // A castle exists to watch the country; a village sees its own fields.
                f32 sight = settlementSight;
                if (settlement->kind == SettlementKind::Castle) sight += castleBonus;
                else if (settlement->kind == SettlementKind::Village) sight *= 0.6f;
                Reveal(world, settlement->position, sight);
            }

            for (EntityId cohortId : clan->cohorts)
            {
                const Cohort* cohort = world.FindCohort(cohortId);
                if (!cohort || cohort->IsEmpty()) continue;
                Reveal(world, cohort->position, cohortSight);
            }
        }

        // Everything inside one's own borders is watched country. Reeves, roadwardens and
        // the villages themselves are eyes enough: a lord does not lose sight of a valley
        // because no banner happens to be standing in it this month. Doing it here rather
        // than by giving every settlement a bigger sight radius is what makes it follow the
        // border as the border moves, which is the whole point.
        RevealOwnLands(world);

        RememberSettlements(world);

        // Blurring a hundred thousand tiles is the most expensive thing this system does,
        // and on most ticks nothing has moved far enough to change a single one of them.
        // Comparing the raw state first is two orders of magnitude cheaper than rebuilding
        // the picture and comparing that.
        if (m_previousState == m_state) return;

        WOC_PROFILE("fog.mask");
        std::vector<u8> mask = BuildMask();
        if (mask != m_lastMask)
        {
            m_lastMask = mask;
            ++m_revision;
            Renderer::Get().SetFogMask(mask, map.TileWidth(), map.TileHeight());
        }
    }

    void FogSystem::RevealOwnLands(World& world)
    {
        const State* state = world.HumanState();
        if (!state) return;

        const MapData& map = world.Map();
        const std::vector<EntityId>& slotToClan = CoverageSystem::Get().SlotToClan();
        if (slotToClan.empty()) return;

        // Which palette slots are ours, resolved once: the inner loop then costs a byte
        // lookup per tile rather than two map searches.
        std::vector<u8> ours(slotToClan.size(), 0);
        for (size_t slot = 1; slot < slotToClan.size(); ++slot)
        {
            const Clan* clan = world.FindClan(slotToClan[slot]);
            if (clan && clan->state == state->id) ours[slot] = 1;
        }

        const std::vector<Tile>& tiles = map.Tiles();
        for (size_t index = 0; index < tiles.size() && index < m_state.size(); ++index)
        {
            const u8 slot = tiles[index].owner;
            if (slot == 0 || slot >= ours.size() || !ours[slot]) continue;

            if (m_state[index] == static_cast<u8>(FogState::Unseen))
            {
                const Coord tile{ static_cast<i32>(index % m_width), static_cast<i32>(index / m_width) };
                if (m_exploredMax.x < m_exploredMin.x)
                {
                    m_exploredMin = tile;
                    m_exploredMax = tile;
                }
                else
                {
                    m_exploredMin = { std::min(m_exploredMin.x, tile.x), std::min(m_exploredMin.y, tile.y) };
                    m_exploredMax = { std::max(m_exploredMax.x, tile.x), std::max(m_exploredMax.y, tile.y) };
                }
                ++m_revision;
            }
            m_state[index] = static_cast<u8>(FogState::Visible);

            if (m_owners[index] != slot)
            {
                m_owners[index] = slot;
                m_dirty = true;
            }
        }
    }

    void FogSystem::RememberSettlements(World& world)
    {
        const i32 today = world.Time().TotalDays();

        for (const auto& [id, settlement] : world.Settlements())
        {
            if (!IsVisible(world, settlement.position)) continue;

            const Clan* owner = world.FindClan(settlement.owner);
            SeenSettlement record;
            record.id = id;
            record.position = settlement.position;
            record.kind = settlement.kind;
            record.sprite = settlement.Sprite();
            record.mirrored = settlement.mirrored;
            record.color = owner ? owner->color : Color::FromRGB(0xB9C0C8);
            record.name = settlement.name;
            record.seenOnDay = today;

            const auto it = std::find_if(m_seen.begin(), m_seen.end(),
                [id](const SeenSettlement& seen) { return seen.id == id; });
            if (it == m_seen.end()) m_seen.push_back(record);
            else *it = record;
        }

        // A town that was razed while you watched is gone from memory too.
        m_seen.erase(std::remove_if(m_seen.begin(), m_seen.end(),
            [&](const SeenSettlement& seen)
            {
                return !world.FindSettlement(seen.id) && IsVisible(world, seen.position);
            }), m_seen.end());
    }

    // =========================================================================================
    // Queries
    // =========================================================================================

    FogState FogSystem::At(const World& world, const Vec2& position) const
    {
        if (!m_enabled) return FogState::Visible;

        const MapData& map = world.Map();
        if (!map.IsValid() || m_state.empty()) return FogState::Visible;

        const Coord tile = map.ToTile(position);
        if (!map.InBounds(tile)) return FogState::Unseen;

        const size_t index = map.Index(tile);
        return index < m_state.size() ? static_cast<FogState>(m_state[index]) : FogState::Unseen;
    }

    bool FogSystem::IsVisible(const World& world, const Vec2& position) const
    {
        return At(world, position) == FogState::Visible;
    }

    bool FogSystem::IsKnown(const World& world, const Vec2& position) const
    {
        return At(world, position) != FogState::Unseen;
    }

    const FogSystem::SeenSettlement* FogSystem::RememberedAt(EntityId settlementId) const
    {
        const auto it = std::find_if(m_seen.begin(), m_seen.end(),
            [settlementId](const SeenSettlement& seen) { return seen.id == settlementId; });
        return it == m_seen.end() ? nullptr : &*it;
    }

    std::vector<u8> FogSystem::BuildMask() const
    {
        std::vector<u8> mask(m_state.size(), 255);
        for (size_t i = 0; i < m_state.size(); ++i)
        {
            switch (static_cast<FogState>(m_state[i]))
            {
            case FogState::Unseen:   mask[i] = 0; break;
            case FogState::Explored: mask[i] = 128; break;
            case FogState::Visible:  mask[i] = 255; break;
            }
        }

        // The states themselves are hard - a tile either was seen or was not - but a hard
        // edge drawn on screen looks like a cut-out. A couple of box passes turn the front
        // into a band a dozen tiles wide, which is what fog actually looks like. Only the
        // picture is blurred; every rule still reads the crisp state underneath.
        const i32 radius = std::max(0, ConfigManager::Get().Int("render/fog/blurTiles", 3));
        if (radius == 0 || m_width < 3 || m_height < 3) return mask;

        std::vector<u8> scratch(mask.size());
        const i32 width = static_cast<i32>(m_width);
        const i32 height = static_cast<i32>(m_height);
        const i32 window = radius * 2 + 1;

        // Separable, and each axis carried on a running sum: the window slides by adding
        // the tile that enters and dropping the one that leaves, so the cost is the same
        // whatever the radius. Summing the window afresh for every tile made this the most
        // expensive thing in a fogged game.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (i32 y = 0; y < height; ++y)
            {
                const u8* row = &mask[static_cast<size_t>(y) * width];
                u8* dst = &scratch[static_cast<size_t>(y) * width];

                i32 total = 0;
                for (i32 k = -radius; k <= radius; ++k) total += row[std::clamp(k, 0, width - 1)];
                for (i32 x = 0; x < width; ++x)
                {
                    dst[x] = static_cast<u8>(total / window);
                    total -= row[std::clamp(x - radius, 0, width - 1)];
                    total += row[std::clamp(x + radius + 1, 0, width - 1)];
                }
            }

            for (i32 x = 0; x < width; ++x)
            {
                i32 total = 0;
                for (i32 k = -radius; k <= radius; ++k)
                {
                    total += scratch[static_cast<size_t>(std::clamp(k, 0, height - 1)) * width + x];
                }
                for (i32 y = 0; y < height; ++y)
                {
                    mask[static_cast<size_t>(y) * width + x] = static_cast<u8>(total / window);
                    total -= scratch[static_cast<size_t>(std::clamp(y - radius, 0, height - 1)) * width + x];
                    total += scratch[static_cast<size_t>(std::clamp(y + radius + 1, 0, height - 1)) * width + x];
                }
            }
        }
        return mask;
    }

    void FogSystem::FilterOwnerMask(std::vector<u8>& mask) const
    {
        if (!m_enabled || m_owners.size() != mask.size()) return;

        // Live borders only where somebody is watching; elsewhere the remembered ones, and
        // nothing at all where the player has never been.
        for (size_t i = 0; i < mask.size(); ++i)
        {
            if (m_state[i] == static_cast<u8>(FogState::Visible)) continue;
            mask[i] = m_state[i] == static_cast<u8>(FogState::Explored) ? m_owners[i] : 0;
        }
    }

    bool FogSystem::ConsumeDirty()
    {
        const bool was = m_dirty;
        m_dirty = false;
        return was;
    }

    // =========================================================================================
    // Persistence
    // =========================================================================================

    Json FogSystem::ToJson() const
    {
        Json node = Json::MakeObject();
        node["enabled"] = m_enabled;
        node["width"] = static_cast<i64>(m_width);
        node["height"] = static_cast<i64>(m_height);

        // Run-length encoded: a fog map is overwhelmingly long stretches of one value.
        Json runs = Json::MakeArray();
        for (size_t i = 0; i < m_state.size();)
        {
            // Visible is not saved as such - on reload nothing is being watched yet, and
            // the next update raises whatever still is.
            const u8 value = m_state[i] == static_cast<u8>(FogState::Unseen) ? 0u : 1u;
            size_t run = 1;
            while (i + run < m_state.size() &&
                   (m_state[i + run] == static_cast<u8>(FogState::Unseen) ? 0u : 1u) == value)
            {
                ++run;
            }
            runs.Push(Json(static_cast<i64>(value)));
            runs.Push(Json(static_cast<i64>(run)));
            i += run;
        }
        node["runs"] = runs;

        Json owners = Json::MakeArray();
        for (size_t i = 0; i < m_owners.size();)
        {
            const u8 value = m_owners[i];
            size_t run = 1;
            while (i + run < m_owners.size() && m_owners[i + run] == value) ++run;
            owners.Push(Json(static_cast<i64>(value)));
            owners.Push(Json(static_cast<i64>(run)));
            i += run;
        }
        node["owners"] = owners;
        return node;
    }

    void FogSystem::FromJson(const Json& node, World& world)
    {
        Reset(world);
        m_enabled = node["enabled"].AsBool(false);
        if (!m_enabled) return;

        auto expand = [](const Json& runs, std::vector<u8>& out)
        {
            size_t cursor = 0;
            for (size_t i = 0; i + 1 < runs.Size() && cursor < out.size(); i += 2)
            {
                const u8 value = static_cast<u8>(runs[i].AsInt(0));
                const size_t run = static_cast<size_t>(runs[i + 1].AsInt(0));
                for (size_t k = 0; k < run && cursor < out.size(); ++k) out[cursor++] = value;
            }
        };

        expand(node["runs"], m_state);
        expand(node["owners"], m_owners);

        // A loaded game has to rediscover how far its own memory reaches; after this the
        // rectangle is kept up to date a tile at a time, as it is during play.
        for (u32 y = 0; y < m_height; ++y)
        {
            for (u32 x = 0; x < m_width; ++x)
            {
                if (m_state[static_cast<size_t>(y) * m_width + x] == static_cast<u8>(FogState::Unseen)) continue;
                const Coord tile{ static_cast<i32>(x), static_cast<i32>(y) };
                if (m_exploredMax.x < m_exploredMin.x) { m_exploredMin = tile; m_exploredMax = tile; continue; }
                m_exploredMin = { std::min(m_exploredMin.x, tile.x), std::min(m_exploredMin.y, tile.y) };
                m_exploredMax = { std::max(m_exploredMax.x, tile.x), std::max(m_exploredMax.y, tile.y) };
            }
        }

        m_dirty = true;
        ++m_revision;
    }
}
