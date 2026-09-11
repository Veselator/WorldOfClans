#include "MarketSystem.h"
#include "EconomySystem.h"
#include "RoadSystem.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <algorithm>
#include <cmath>

namespace woc
{
    namespace
    {
        /// Where a good's price starts, in silver per unit, before any trading has happened.
        f32 SeedPrice(ResourceType resource)
        {
            ConfigManager& config = ConfigManager::Get();
            switch (resource)
            {
            case ResourceType::Wood:  return config.Float("market/basePrice/wood", 2.2f);
            case ResourceType::Stone: return config.Float("market/basePrice/stone", 4.5f);
            case ResourceType::Food:  return config.Float("market/basePrice/food", 1.6f);
            default:                  return 1.0f;
            }
        }
    }

    size_t MarketSystem::Index(ResourceType resource)
    {
        return std::min(static_cast<size_t>(resource), kGoods - 1);
    }

    ResourceType MarketSystem::Resource(size_t index)
    {
        return static_cast<ResourceType>(std::min(index, kGoods - 1));
    }

    const char* MarketSystem::ResourceName(ResourceType resource)
    {
        switch (resource)
        {
        case ResourceType::Money: return "Срібло";
        case ResourceType::Wood:  return "Дерево";
        case ResourceType::Stone: return "Камінь";
        default:                  return "Їжа";
        }
    }

    bool MarketSystem::HasMarket(const Settlement& settlement)
    {
        return settlement.HasBuilding(ConfigManager::Get().Str("market/building", "market"));
    }

    void MarketSystem::Reset()
    {
        for (size_t i = 0; i < kGoods; ++i)
        {
            m_price[i] = SeedPrice(Resource(i));
            m_trend[i] = 0.0f;
            m_pressure[i] = 0.0f;
        }
        m_price[Index(ResourceType::Money)] = 1.0f;
        m_seeded = true;
    }

    f32 MarketSystem::Price(ResourceType resource) const
    {
        if (resource == ResourceType::Money) return 1.0f;
        return m_price[Index(resource)];
    }

    f32 MarketSystem::Trend(ResourceType resource) const
    {
        return resource == ResourceType::Money ? 0.0f : m_trend[Index(resource)];
    }

    f32 MarketSystem::Pressure(ResourceType resource) const
    {
        return resource == ResourceType::Money ? 0.0f : m_pressure[Index(resource)];
    }

    // =========================================================================================
    // Pricing
    // =========================================================================================

    void MarketSystem::Tick(World& world, i32 days)
    {
        if (!m_seeded) Reset();
        if (days <= 0) return;

        ConfigManager& config = ConfigManager::Get();
        const f32 elasticity = config.Float("market/elasticity", 0.22f);
        const f32 scale = config.Float("market/pressureScale", 45.0f);
        const f32 floorFactor = config.Float("market/priceFloor", 0.35f);
        const f32 ceilFactor = config.Float("market/priceCeiling", 3.4f);
        const f32 monthDays = static_cast<f32>(std::max(1, config.Int("simulation/daysPerMonth", 30)));

        // --- who is on the market, and what they need ----------------------------------------
        // Only realms with a market trade at all. A realm's surplus of a good is what it can
        // offer; its shortfall is what it will bid for. A stock already piled high counts a
        // little towards supply too - a granary bursting with grain depresses the price of
        // grain whether or not its owner means to sell.
        std::array<f32, kGoods> supply{};
        std::array<f32, kGoods> demand{};
        supply.fill(0.0f);
        demand.fill(0.0f);

        for (const auto& [clanId, clan] : world.Clans())
        {
            if (clan.eliminated) continue;

            bool trades = false;
            f32 reach = 0.0f;   // how much weight this realm's wants carry
            for (EntityId settlementId : clan.settlements)
            {
                const Settlement* settlement = world.FindSettlement(settlementId);
                if (!settlement || !HasMarket(*settlement)) continue;
                trades = true;
                reach += 0.5f + settlement->ProsperityFraction();
            }
            if (!trades) continue;

            const ClanBudget budget = EconomySystem::Get().Preview(world, clanId);
            for (size_t i = 0; i < kGoods; ++i)
            {
                const ResourceType resource = Resource(i);
                if (resource == ResourceType::Money) continue;

                const f32 net = budget.net[resource];
                const f32 stock = clan.resources[resource];
                const f32 weight = std::min(reach, config.Float("market/reachCap", 4.0f));

                if (net >= 0.0f) supply[i] += (net + stock * 0.01f) * weight;
                else             demand[i] += (-net) * weight;
            }
        }

        // --- move the prices ------------------------------------------------------------------
        const f32 step = static_cast<f32>(days) / monthDays;

        for (size_t i = 0; i < kGoods; ++i)
        {
            const ResourceType resource = Resource(i);
            if (resource == ResourceType::Money) { m_price[i] = 1.0f; continue; }

            // Net demand, normalised so that "one realm badly short of this" is a figure
            // around one rather than an arbitrary number of units per month.
            const f32 net = (demand[i] - supply[i]) / std::max(1.0f, scale);
            m_pressure[i] = net;

            const f32 seed = SeedPrice(resource);
            const f32 before = m_price[i];

            // Towards the price the pressure implies, never all the way in one step: a
            // market that jumped straight to equilibrium would have no history to read.
            const f32 target = seed * std::clamp(1.0f + net, floorFactor, ceilFactor);
            m_price[i] += (target - m_price[i]) * std::clamp(elasticity * step, 0.0f, 1.0f);
            m_price[i] = std::clamp(m_price[i], seed * floorFactor, seed * ceilFactor);

            m_trend[i] = m_price[i] - before;
        }
    }

    // =========================================================================================
    // Trading
    // =========================================================================================

    MarketSystem::Quote MarketSystem::QuoteAt(World& world, EntityId settlementId,
                                              ResourceType resource) const
    {
        Quote quote;
        const f32 price = Price(resource);
        quote.buy = price;
        quote.sell = price;

        // Silver is the measure, not a good on the stall. Nobody charges a commission for
        // handing over coin, and charging one would tax both legs of every sale twice.
        if (resource == ResourceType::Money)
        {
            quote.spread = 0.0f;
            return quote;
        }

        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return quote;

        ConfigManager& config = ConfigManager::Get();
        f32 spread = config.Float("market/baseSpread", 0.18f);

        // A great city's merchants are many and compete with one another; a village market
        // is one man with a scale and no reason to be generous.
        spread -= settlement->ProsperityFraction() * config.Float("market/spreadProsperity", 0.06f);
        spread -= static_cast<f32>(settlement->TierIndex()) * config.Float("market/spreadTier", 0.02f);
        if (settlement->HasBuilding("harbor")) spread -= config.Float("market/spreadHarbor", 0.04f);

        // Roads bring other people's goods here, and competition narrows the cut further:
        // a market a neighbour's carts can reach is a market with rivals in it.
        const Clan* owner = world.FindClan(settlement->owner);
        if (owner)
        {
            for (EntityId otherId : owner->settlements)
            {
                if (otherId == settlementId) continue;
                if (!RoadSystem::Get().AreConnected(world, settlementId, otherId)) continue;
                spread -= config.Float("market/spreadRoad", 0.03f);
                break;
            }
        }

        quote.spread = std::clamp(spread, config.Float("market/minSpread", 0.04f),
                                  config.Float("market/maxSpread", 0.35f));
        quote.buy = price * (1.0f + quote.spread);
        quote.sell = price * (1.0f - quote.spread);
        return quote;
    }

    f32 MarketSystem::Impact(ResourceType resource, f32 amount) const
    {
        if (resource == ResourceType::Money) return 0.0f;

        // How much one trade shifts a price. Square-rooted, so the first hundred logs move
        // the market noticeably and the tenth hundred barely more - which is how depth in a
        // real market behaves.
        const f32 depth = std::max(1.0f, ConfigManager::Get().Float("market/depth", 260.0f));
        return std::sqrt(std::max(0.0f, amount) / depth);
    }

    f32 MarketSystem::Preview(World& world, EntityId settlementId, ResourceType from,
                              ResourceType to, f32 amount) const
    {
        if (amount <= 0.0f || from == to) return 0.0f;

        const Quote give = QuoteAt(world, settlementId, from);
        const Quote take = QuoteAt(world, settlementId, to);

        // Selling depresses what you are selling and bidding up what you are buying. Both
        // are charged at the average price across the trade, not at the price before it.
        const f32 slipOut = 1.0f - std::min(0.6f, Impact(from, amount) * 0.5f);
        const f32 silver = amount * give.sell * slipOut;

        const f32 wanted = silver / std::max(0.0001f, take.buy);
        const f32 slipIn = 1.0f + std::min(0.6f, Impact(to, wanted) * 0.5f);
        return silver / std::max(0.0001f, take.buy * slipIn);
    }

    f32 MarketSystem::Trade(World& world, EntityId settlementId, EntityId clanId,
                            ResourceType from, ResourceType to, f32 amount)
    {
        if (amount <= 0.0f || from == to) return 0.0f;

        Clan* clan = world.FindClan(clanId);
        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!clan || !settlement || !HasMarket(*settlement)) return 0.0f;
        if (clan->resources[from] < amount) return 0.0f;

        const f32 received = Preview(world, settlementId, from, to, amount);
        if (received <= 0.0f) return 0.0f;

        clan->resources[from] -= amount;
        clan->resources[to] += received;

        // The trade leaves its mark on the price: what was sold is now commoner, what was
        // bought scarcer. This is the whole reason a player cannot cycle two goods for
        // free silver.
        const f32 bite = ConfigManager::Get().Float("market/tradeImpact", 0.6f);
        if (from != ResourceType::Money)
        {
            const size_t i = Index(from);
            m_price[i] = std::max(SeedPrice(from) * ConfigManager::Get().Float("market/priceFloor", 0.35f),
                                  m_price[i] * (1.0f - Impact(from, amount) * bite));
        }
        if (to != ResourceType::Money)
        {
            const size_t i = Index(to);
            m_price[i] = std::min(SeedPrice(to) * ConfigManager::Get().Float("market/priceCeiling", 3.4f),
                                  m_price[i] * (1.0f + Impact(to, received) * bite));
        }

        return received;
    }

    // =========================================================================================
    // Persistence
    // =========================================================================================

    Json MarketSystem::ToJson() const
    {
        Json node = Json::MakeObject();
        Json prices = Json::MakeArray();
        for (size_t i = 0; i < kGoods; ++i) prices.Push(Json(m_price[i]));
        node["prices"] = prices;
        return node;
    }

    void MarketSystem::FromJson(const Json& node)
    {
        Reset();
        const Json& prices = node["prices"];
        for (size_t i = 0; i < kGoods && i < prices.Size(); ++i)
        {
            m_price[i] = prices[i].AsFloat(m_price[i]);
        }
        m_price[Index(ResourceType::Money)] = 1.0f;
    }
}
