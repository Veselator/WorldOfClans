// EconomySystem.h - monthly production, upkeep and the consequences of running dry.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/ResourceData.h"

namespace woc
{
    class World;
    class Clan;

    /// A breakdown the interface can show: where the month's income came from and went.
    struct ClanBudget
    {
        ResourceData income;
        ResourceData upkeep;
        ResourceData net;
        f32 foodConsumption = 0.0f;
        bool starving = false;
    };

    class EconomySystem final : public Singleton<EconomySystem>
    {
        friend class Singleton<EconomySystem>;
    public:
        /// Applies `days` worth of economy to every clan. The figures the panels show are
        /// still monthly - that is how a player thinks about income - but the treasury is
        /// settled on whatever cadence the simulation runs it at.
        void Tick(World& world, i32 days);

        /// Computes a clan's budget without applying it - used by the treasury panel.
        ClanBudget Preview(World& world, EntityId clanId) const;

        /// Monthly output of a single settlement, including its buildings.
        ResourceData SettlementOutput(World& world, EntityId settlementId) const;

        /// What the clan owes each month for garrisons, castles and armies.
        ResourceData ClanUpkeep(World& world, EntityId clanId) const;

    private:
        EconomySystem() = default;
        ~EconomySystem() = default;

        void ApplyShortages(World& world, Clan& clan, const ClanBudget& budget);
    };
}
