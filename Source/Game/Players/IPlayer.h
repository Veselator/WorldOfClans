// IPlayer.h - the seat behind a realm.
//
// The simulation never asks "is this the player?"; it asks a seat to take its turn.
// HumanPlayer defers to the interface, AIPlayer runs its own evaluation.
#pragma once
#include "../../Core/Json.h"

#include "../../Core/Types.h"

namespace woc
{
    class World;

    class IPlayer
    {
    public:
        explicit IPlayer(EntityId stateId) : m_stateId(stateId) {}
        virtual ~IPlayer() = default;

        EntityId StateId() const { return m_stateId; }

        virtual bool IsHuman() const = 0;
        virtual const char* Kind() const = 0;

        /// Called once for every simulated day the realm is alive.
        virtual void OnDay(World& world, i32 day) { (void)world; (void)day; }
        /// Called on the AI cadence configured in game.json.
        virtual void OnThink(World& world, i32 day) { (void)world; (void)day; }

        /// Answers an embassy from `from` on the spot. A seat that cannot answer at once - a
        /// person, who is asked in a dialog instead - declines by default.
        virtual bool WeighOffer(const World& world, EntityId from, i32 kind) const
        {
            (void)world; (void)from; (void)kind;
            return false;
        }

        /// What the player keeps in its head between thinks, for saves and resynchronisation.
        virtual Json ToJson() const { return Json::MakeObject(); }
        virtual void FromJson(const Json& node) { (void)node; }

    protected:
        EntityId m_stateId = kInvalidId;
    };
}
