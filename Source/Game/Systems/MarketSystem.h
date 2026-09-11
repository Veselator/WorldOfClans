// MarketSystem.h - one price for each good, and the exchange that sets it.
//
// The model is the simplest one that behaves like a market rather than like a shop:
//
//   * every good has a price in silver, which is the numéraire and always costs itself;
//   * every realm that owns a market puts its surplus on the market and takes its deficit
//     off it. A land of ploughmen with no quarry bids stone up; a land of quarries and no
//     fields bids grain up. Realms with no market are not in the exchange at all, which is
//     what makes building one worth doing;
//   * prices move towards where supply and demand balance, a little each fortnight, and
//     never past a floor or a ceiling - a famine can treble the price of grain, it cannot
//     make it worth a castle;
//   * a trade moves the price by itself. Dumping five hundred logs on the market gets you
//     a worse rate for the last hundred than for the first, because you have just made
//     logs less scarce. This is what stops the exchange being an infinite silver pump;
//   * the house takes a cut. The cut narrows in a rich town, on a road, or at a harbour -
//     which is the whole reason to trade in a great city rather than a village.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Json.h"
#include "../World/ResourceData.h"

#include <array>
#include <string>

namespace woc
{
    class World;
    class Settlement;

    class MarketSystem final : public Singleton<MarketSystem>
    {
        friend class Singleton<MarketSystem>;
    public:
        /// The two sides of a price at one particular market: what the merchants there will
        /// pay you for a unit, and what they will charge you for one. Buy is always dearer
        /// than sell; the gap is the house's cut.
        struct Quote
        {
            f32 buy = 1.0f;    // silver you pay for one unit
            f32 sell = 1.0f;   // silver you receive for one unit
            f32 spread = 0.1f;
        };

        void Reset();
        /// Re-prices every good. Called on the economy's own cadence.
        void Tick(World& world, i32 days);

        /// True when this settlement can trade at all.
        static bool HasMarket(const Settlement& settlement);

        /// The world price of a good in silver, before any market's cut.
        f32 Price(ResourceType resource) const;
        /// Which way the price has been moving: positive means dearer.
        f32 Trend(ResourceType resource) const;
        /// Net demand across the trading realms - what is actually pushing the price.
        f32 Pressure(ResourceType resource) const;

        /// The quote at one particular market.
        Quote QuoteAt(World& world, EntityId settlementId, ResourceType resource) const;

        /// What `amount` of `from` would fetch in `to` at this market, the merchants' cut
        /// and the price the trade itself moves both taken into account.
        f32 Preview(World& world, EntityId settlementId, ResourceType from, ResourceType to,
                    f32 amount) const;

        /// Performs the exchange. Returns how much of `to` the clan actually received; 0
        /// means nothing happened.
        f32 Trade(World& world, EntityId settlementId, EntityId clanId, ResourceType from,
                  ResourceType to, f32 amount);

        /// Saved with the party: a market that forgot its prices on every load would be
        /// an odd sort of market.
        Json ToJson() const;
        void FromJson(const Json& node);

        static const char* ResourceName(ResourceType resource);
        static constexpr size_t kGoods = 4;

    private:
        MarketSystem() = default;
        ~MarketSystem() = default;

        static size_t Index(ResourceType resource);
        static ResourceType Resource(size_t index);
        /// How hard a trade of this size shifts the price of a good.
        f32 Impact(ResourceType resource, f32 amount) const;

        /// Silver per unit. Money is 1 by definition and never moves.
        std::array<f32, kGoods> m_price{ { 1.0f, 1.0f, 1.0f, 1.0f } };
        std::array<f32, kGoods> m_trend{ { 0.0f, 0.0f, 0.0f, 0.0f } };
        std::array<f32, kGoods> m_pressure{ { 0.0f, 0.0f, 0.0f, 0.0f } };
        bool m_seeded = false;
    };
}
