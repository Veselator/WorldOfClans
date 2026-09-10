// CoverageSystem.h - the dynamic borders.
//
// Cities and castles project authority outwards through the terrain using the same
// Dijkstra flood the pathfinder provides. Whichever settlement reaches a tile most
// cheaply, relative to its own budget, owns it - so borders bend around hills, stop at
// wide rivers and stretch along roads, instead of being drawn as circles.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"

namespace woc
{
    class World;

    class CoverageSystem final : public Singleton<CoverageSystem>
    {
        friend class Singleton<CoverageSystem>;
    public:
        /// Recomputes the whole ownership field and refreshes the renderer mask.
        void Recompute(World& world);

        /// Cheap check used by the UI and the AI: who holds this map position.
        EntityId OwnerAt(const World& world, const Vec2& position) const;

        /// Palette slot -> clan, so the renderer's owner mask can be decoded.
        const std::vector<EntityId>& SlotToClan() const { return m_slotToClan; }
        /// Total covered tiles per clan, for the realm overview panel.
        u32 TilesOwnedBy(EntityId clanId) const;

        bool IsDirty() const { return m_dirty; }
        void MarkDirty() { m_dirty = true; }
        void ClearDirty() { m_dirty = false; }

    private:
        CoverageSystem() = default;
        ~CoverageSystem() = default;

        void AssignPaletteSlots(World& world);
        void AbsorbIndependentVillages(World& world);
        void ReleaseUncoveredVillages(World& world);

        std::vector<EntityId> m_slotToClan;      // index = palette slot
        std::vector<u32> m_tilesPerSlot;
        std::vector<f32> m_bestScore;            // per tile, lowest normalised coverage cost
        std::vector<EntityId> m_scratchOwner;    // per tile, claiming settlement
        bool m_dirty = true;
    };
}
