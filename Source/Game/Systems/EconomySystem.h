// EconomySystem.h - monthly production, upkeep and the consequences of running dry.
#pragma once

#include "../../Core/Singleton.h"
#include "../World/ResourceData.h"

#include <string>

namespace woc
{
    class World;
    class Clan;
    class Settlement;

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
        /// The same sum for a settlement that need not be in the world - which is how the
        /// interface asks "and without this building?" of a copy with the building taken out.
        ResourceData SettlementOutput(const World& world, const Settlement& settlement) const;
        /// What one standing building adds to its settlement's monthly output.
        ResourceData BuildingContribution(const World& world, const Settlement& settlement,
                                          const std::string& buildingId) const;

        /// What the clan owes each month for garrisons, castles and armies.
        ResourceData ClanUpkeep(World& world, EntityId clanId) const;

    private:
        EconomySystem() = default;
        ~EconomySystem() = default;

        void ApplyShortages(World& world, Clan& clan, const ClanBudget& budget);
    };
}
