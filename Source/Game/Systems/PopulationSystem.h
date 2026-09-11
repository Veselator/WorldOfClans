// PopulationSystem.h - people: growth, loyalty, migration and revolt.
//
// New villages are not placed by a designer; they appear when enough migrants accumulate
// and there is somewhere sensible for them to go.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"

namespace woc
{
    class World;
    class Settlement;

    class PopulationSystem final : public Singleton<PopulationSystem>
    {
        friend class Singleton<PopulationSystem>;
    public:
        void Tick(World& world);

        /// Loyalty change this settlement would see next month, for the info panel.
        f32 LoyaltyForecast(World& world, EntityId settlementId) const;

    private:
        PopulationSystem() = default;
        ~PopulationSystem() = default;

        void GrowPopulation(World& world, Settlement& settlement);
        void UpdateProsperity(World& world, Settlement& settlement);
        void UpdateLoyalty(World& world, Settlement& settlement);
        void HandleRevolts(World& world);
        /// The odds this settlement throws off its lord within a month, 0..1.
        f32 RevoltChance(World& world, const Settlement& settlement) const;
        void SpawnVillages(World& world);

        /// Migrants pooled per clan until they are numerous enough to found a village.
        std::vector<std::pair<EntityId, f32>> m_migrantPool;

        f32& MigrantsFor(EntityId clanId);
    };
}
