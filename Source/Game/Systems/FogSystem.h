// FogSystem.h - what the player's realm has seen of the world.
//
// Three zones, and the difference between them is the difference between knowing, having
// known, and never having looked:
//
//   Visible    - somebody of yours is standing there now. Everything is live: borders,
//                armies, the state of every town.
//   Explored   - you were there once. The land is drawn dimmed and the settlements are
//                drawn as you last saw them, banner and all. Armies are not drawn: you
//                remember where a town was, not where a column happens to be today.
//   Unseen     - never looked. Nothing but fog, drawn by its own shader.
//
// The memory is per tile, and the owner mask is remembered alongside it so that a frontier
// you walked past stays on the map exactly as it was when you walked past it.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"
#include "../World/Settlement.h"

#include <string>
#include <vector>

namespace woc
{
    class World;

    enum class FogState : u8
    {
        Unseen = 0,
        Explored = 1,
        Visible = 2
    };

    class FogSystem final : public Singleton<FogSystem>
    {
        friend class Singleton<FogSystem>;
    public:
        /// A settlement as the player last laid eyes on it.
        struct SeenSettlement
        {
            EntityId id = kInvalidId;
            Vec2 position;
            SettlementKind kind = SettlementKind::Village;
            SpriteId sprite = SpriteId::Village;
            bool mirrored = false;
            Color color{ 0.7f, 0.75f, 0.8f, 1.0f };
            std::string name;
            i32 seenOnDay = 0;
        };

        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; m_dirty = true; }

        /// Sizes the memory to the map and forgets everything. Called when a party starts.
        void Reset(World& world);
        /// Recomputes what is visible and folds it into the memory. Cheap; per frame is fine.
        void Update(World& world, f32 realSeconds);

        FogState At(const World& world, const Vec2& position) const;
        bool IsVisible(const World& world, const Vec2& position) const;
        /// True when the player may be shown *something* here - live or remembered.
        bool IsKnown(const World& world, const Vec2& position) const;

        const std::vector<SeenSettlement>& Remembered() const { return m_seen; }
        /// The settlement the player remembers here, or null if he never saw one.
        const SeenSettlement* RememberedAt(EntityId settlementId) const;

        /// The fog layer as the renderer wants it: 0 never seen, 128 seen once, 255 now.
        std::vector<u8> BuildMask() const;
        /// Rises every time the memory actually changed. Anything drawn from the fog can
        /// compare it against what it last drew and skip the work when nothing moved.
        u64 Revision() const { return m_revision; }
        /// The tiles the realm has ever seen, as a rectangle in tiles. Empty (max < min)
        /// while nothing has been seen. Maintained as the memory changes rather than
        /// scanned for, because scanning a hundred thousand tiles to place a picture in
        /// the corner of the screen is not a thing to do several times a second.
        Coord ExploredMin() const { return m_exploredMin; }
        Coord ExploredMax() const { return m_exploredMax; }

        /// The raw memory, one FogState per tile. The minimap reads it directly: it has to
        /// know not only where it may draw but how far the known world actually reaches.
        const std::vector<u8>& States() const { return m_state; }
        u32 Width() const { return m_width; }
        u32 Height() const { return m_height; }
        /// Rewrites a freshly built owner mask into what the player is entitled to see.
        void FilterOwnerMask(std::vector<u8>& mask) const;

        /// Raised when the memory changed enough that the coloured layer should be redrawn.
        bool ConsumeDirty();

        /// Saved with the party so a reloaded game does not forget the map.
        Json ToJson() const;
        void FromJson(const Json& node, World& world);

    private:
        FogSystem() = default;
        ~FogSystem() = default;

        void Reveal(const World& world, const Vec2& centre, f32 radius);
        /// Raises every tile the player's own realm holds to "watched". A border that moves
        /// therefore takes the fog with it, which a fixed sight radius per town never did.
        void RevealOwnLands(World& world);
        void RememberSettlements(World& world);

        std::vector<u8> m_state;        // per tile, FogState
        std::vector<u8> m_owners;       // per tile, the owner slot as last seen
        std::vector<SeenSettlement> m_seen;
        std::vector<u8> m_lastMask;
        std::vector<u8> m_previousState;   // scratch: what the memory looked like last tick

        u64 m_revision = 1;
        Coord m_exploredMin{ 0, 0 };
        Coord m_exploredMax{ -1, -1 };

        u32 m_width = 0;
        u32 m_height = 0;
        f32 m_timer = 0.0f;
        bool m_enabled = false;
        bool m_dirty = true;
    };
}
