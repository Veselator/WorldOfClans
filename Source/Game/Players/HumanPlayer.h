// HumanPlayer.h - the seat the interface drives.
#pragma once

#include "IPlayer.h"

namespace woc
{
    class HumanPlayer final : public IPlayer
    {
    public:
        explicit HumanPlayer(EntityId stateId) : IPlayer(stateId) {}

        bool IsHuman() const override { return true; }
        const char* Kind() const override { return "human"; }

        // Every decision arrives through the UI, so there is nothing to do per day.
    };
}
