// DiplomacySystem.h - opinions, pacts, wars and marriages between realms.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/State.h"

namespace woc
{
    class World;

    /// One offer the player or the AI can make. Kept as data so the interface can list
    /// every available action without knowing what each of them does.
    struct DiplomaticAction
    {
        enum class Kind { DeclareWar, OfferPeace, NonAggression, Alliance, ArrangeMarriage, BreakPact };
        Kind kind = Kind::DeclareWar;
        std::string label;
        std::string tooltip;
        bool available = true;
        f32 opinionCost = 0.0f;
    };

    class DiplomacySystem final : public Singleton<DiplomacySystem>
    {
        friend class Singleton<DiplomacySystem>;
    public:
        /// Monthly opinion drift, truce expiry and AI-initiated diplomacy.
        void Tick(World& world);

        std::vector<DiplomaticAction> AvailableActions(World& world, EntityId from, EntityId to) const;
        bool Perform(World& world, EntityId from, EntityId to, DiplomaticAction::Kind kind);

        void DeclareWar(World& world, EntityId from, EntityId to);
        void MakePeace(World& world, EntityId from, EntityId to);
        void SignNonAggression(World& world, EntityId from, EntityId to);
        void FormAlliance(World& world, EntityId from, EntityId to);
        bool ArrangeMarriage(World& world, EntityId from, EntityId to);

        /// How much `to` likes `from`, including the standing modifiers.
        f32 Opinion(const World& world, EntityId from, EntityId to) const;
        /// Human-readable breakdown for the diplomacy panel.
        std::vector<std::pair<std::string, f32>> OpinionBreakdown(const World& world,
                                                                 EntityId from, EntityId to) const;

    private:
        DiplomacySystem() = default;
        ~DiplomacySystem() = default;

        void SetStance(World& world, EntityId a, EntityId b, DiplomaticStance stance, i32 untilDay);
        void AdjustOpinion(World& world, EntityId a, EntityId b, f32 delta);
    };
}
