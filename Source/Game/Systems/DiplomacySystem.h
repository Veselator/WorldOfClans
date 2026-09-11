// DiplomacySystem.h - opinions, pacts, wars and marriages between realms.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/State.h"

#include <deque>

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

    /// Something another realm is asking of the player and waiting on an answer to. The AI
    /// signs pacts with other AIs outright; with the player it has to ask, because a pact
    /// nobody agreed to is not a pact.
    struct DiplomaticOffer
    {
        EntityId from = kInvalidId;
        EntityId to = kInvalidId;
        DiplomaticAction::Kind kind = DiplomaticAction::Kind::NonAggression;
        i32 dayMade = 0;
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

        // --- offers waiting on the player -------------------------------------------------
        bool HasOffer() const { return !m_offers.empty(); }
        const DiplomaticOffer& FrontOffer() const { return m_offers.front(); }
        /// Signs whatever was offered and clears it from the queue.
        void AcceptOffer(World& world);
        /// Turns it down. The asker takes it a little to heart, as anyone would.
        void DeclineOffer(World& world);
        void ClearOffers() { m_offers.clear(); }
        /// What the offer is called, for the dialog that asks about it.
        static const char* OfferTitle(DiplomaticAction::Kind kind);
        static const char* OfferBody(DiplomaticAction::Kind kind);

        /// How much `to` likes `from`, including the standing modifiers.
        f32 Opinion(const World& world, EntityId from, EntityId to) const;
        /// Human-readable breakdown for the diplomacy panel.
        std::vector<std::pair<std::string, f32>> OpinionBreakdown(const World& world,
                                                                 EntityId from, EntityId to) const;

    private:
        DiplomacySystem() = default;
        ~DiplomacySystem() = default;

        void SetStance(World& world, EntityId a, EntityId b, DiplomaticStance stance, i32 untilDay);
        /// Queues an offer for the player, unless the same one is already waiting.
        void Propose(World& world, EntityId from, EntityId to, DiplomaticAction::Kind kind);

        std::deque<DiplomaticOffer> m_offers;
        void AdjustOpinion(World& world, EntityId a, EntityId b, f32 delta);
    };
}
