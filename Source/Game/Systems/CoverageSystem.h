// CoverageSystem.h - the dynamic borders, and the thematic map layers built on them.
//
// Cities and castles project authority outwards through the terrain using a Dijkstra
// flood. Whichever settlement reaches a tile most cheaply, relative to its own budget,
// owns it - so borders bend around hills, stop at wide rivers and stretch along roads,
// instead of being drawn as circles.
//
// The flood is a *single multi-source* pass: every seat is pushed into one queue with its
// score already normalised by its own budget, so the whole field costs one Dijkstra over
// the map rather than one per settlement plus a full-map scan each. What is left of the
// cost is handed to a worker thread, and the result is applied on the next frame that
// finds it ready - the simulation never blocks on the borders.
//
// Expansion is not free: a tile that already answers to a lord can only be taken by
// someone at war with him. A realm therefore grows into open country and into enemy
// land, never quietly into a neighbour's at peace.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"

#include <future>
#include <string>
#include <vector>

namespace woc
{
    class World;
    class MapData;

    /// What the coloured layer over the terrain is showing.
    enum class MapMode : u8
    {
        Realms,      // one colour per clan - the political map
        Influence,   // one colour per seat - who actually holds each stretch of country
        Races,       // the peoples
        Faiths       // the gods
    };

    class CoverageSystem final : public Singleton<CoverageSystem>
    {
        friend class Singleton<CoverageSystem>;
    public:
        /// Starts a recompute on a worker thread if none is running. Cheap; call freely.
        void Recompute(World& world);
        /// Recomputes here and now. For loading, world generation and the editor, where the
        /// caller genuinely needs the borders before it returns.
        void RecomputeBlocking(World& world);
        /// Applies a finished background recompute. Call once per frame.
        void Update(World& world);

        /// Cheap check used by the UI and the AI: who holds this map position.
        EntityId OwnerAt(const World& world, const Vec2& position) const;
        /// Which seat's zone of influence a position falls in.
        EntityId HolderAt(const World& world, const Vec2& position) const;
        /// Every settlement standing inside that seat's zone of influence.
        std::vector<EntityId> SubjectsOf(const World& world, EntityId settlementId) const;
        /// True while this settlement stands inside somebody's zone of control.
        bool IsCovered(const World& world, const Vec2& position) const;

        /// Palette slot -> clan, so the renderer's owner mask can be decoded.
        const std::vector<EntityId>& SlotToClan() const { return m_slotToClan; }
        /// Total covered tiles per clan, for the realm overview panel.
        u32 TilesOwnedBy(EntityId clanId) const;

        // --- map modes ------------------------------------------------------------------
        MapMode Mode() const { return m_mode; }
        void SetMode(MapMode mode, World& world);
        /// Rebuilds the coloured layer for the current mode and hands it to the renderer.
        void RefreshLayer(World& world);
        /// The layer exactly as the terrain shader has it: one palette slot per tile, and
        /// the palette to decode it. The minimap draws from these so that it shows whatever
        /// map mode the player is looking at without knowing what a mode is.
        const std::vector<u8>& Layer() const { return m_lastMask; }
        /// Rises whenever that layer actually changed.
        u64 LayerRevision() const { return m_layerRevision; }
        const std::vector<Color>& LayerPalette() const { return m_lastPalette; }
        static const char* ModeName(MapMode mode);

        bool IsDirty() const { return m_dirty; }
        void MarkDirty() { m_dirty = true; }
        void ClearDirty() { m_dirty = false; }

    private:
        CoverageSystem() = default;
        ~CoverageSystem();

        /// One seat's claim on the countryside, in the form the worker needs.
        struct Seed
        {
            EntityId settlement = kInvalidId;
            u32 origin = 0;         // tile index
            f32 budget = 1.0f;      // coverage strength x costPerUnit
            u8 clanSlot = 0;        // the owning clan's palette slot
        };

        /// Everything the flood needs, detached from the world so it can run off-thread.
        struct Job
        {
            u32 width = 0;
            u32 height = 0;
            f32 coreCost = 12.0f;
            u32 maxNodes = 0;
            u8 clanSlots = 1;

            std::vector<f32> stepCost;      // per tile; negative means impassable
            std::vector<u8> heldBy;         // per tile, the clan slot that holds it today
            std::vector<u8> mayEnter;       // [seedSlot * clanSlots + heldSlot]
            std::vector<Seed> seeds;
            std::vector<EntityId> owner;    // per tile, the winning settlement (output)
        };

        void AssignPaletteSlots(World& world);
        void AbsorbIndependentVillages(World& world);

        Job BuildJob(World& world);
        static void RunJob(Job& job);
        void ApplyJob(World& world, const Job& job);

        std::vector<EntityId> m_slotToClan;       // index = clan palette slot
        std::vector<EntityId> m_slotToHolder;     // index = influence slot -> settlement
        std::vector<u32> m_tilesPerSlot;
        std::vector<u8> m_lastMask;               // last mask handed to the GPU
        std::vector<Color> m_lastPalette;         // and the colours that decode it
        u64 m_layerRevision = 1;
        std::future<Job> m_pending;
        /// The last finished job, kept only for its allocations.
        Job m_scratch;
        MapMode m_mode = MapMode::Realms;
        bool m_busy = false;
        bool m_dirty = true;
    };
}
