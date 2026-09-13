#include "Simulation.h"

#include <cstdlib>

#include "BanditSystem.h"
#include "BattleSystem.h"
#include "CoverageSystem.h"
#include "../../Render/Renderer.h"
#include "FogSystem.h"
#include "DiplomacySystem.h"
#include "DynastySystem.h"
#include "EconomySystem.h"
#include "MarketSystem.h"
#include "ForestrySystem.h"
#include "RoadSystem.h"
#include "MovementSystem.h"
#include "PoliticsSystem.h"
#include "PopulationSystem.h"
#include "SettlementSystem.h"
#include "../Players/IPlayer.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Profiler.h"

#include <algorithm>

namespace woc
{
    void Simulation::Configure()
    {
        ConfigManager& config = ConfigManager::Get();

        m_speedSteps.clear();
        for (const Json& step : config.Game().Get("simulation/speedSteps").AsArray())
        {
            m_speedSteps.push_back(step.AsFloat(1.0f));
        }
        if (m_speedSteps.empty()) m_speedSteps = { 0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };

        m_speedIndex = std::clamp(config.Int("simulation/defaultSpeedIndex", 2), 0,
                                  static_cast<i32>(m_speedSteps.size()) - 1);
        m_previousSpeedIndex = std::max(1, m_speedIndex);

        m_daysPerSecond = config.Float("simulation/daysPerSecond", 1.0f);
        m_tickDays = std::max(0.001f, config.Float("simulation/tickDays", 1.0f / 24.0f));
        m_economyTickDays = config.Int("simulation/economyTickDays", 30);
        m_populationTickDays = config.Int("simulation/populationTickDays", 30);
        m_forestryTickDays = config.Int("simulation/forestryTickDays", 30);
        m_aiTickDays = config.Int("simulation/aiTickDays", 15);
        m_coverageTickDays = config.Int("coverage/recomputeIntervalDays", 15);
        m_daysPerYear = config.Int("simulation/daysPerMonth", 30) * config.Int("simulation/monthsPerYear", 12);
    }

    void Simulation::Reset()
    {
        m_lastEconomyDay = 0;
        m_lastPopulationDay = 0;
        m_lastForestryDay = 0;
        m_lastAIDay = 0;
        m_lastCoverageDay = 0;
        m_lastYearDay = 0;
        m_tick = 0;
        m_carryDays = 0.0f;
        PoliticsSystem::Get().Reset();
        Configure();
    }

    f32 Simulation::SpeedMultiplier() const
    {
        if (m_speedSteps.empty()) return 1.0f;
        return m_speedSteps[static_cast<size_t>(std::clamp(m_speedIndex, 0,
                            static_cast<i32>(m_speedSteps.size()) - 1))];
    }

    bool Simulation::IsPaused() const
    {
        return SpeedMultiplier() <= 0.0f;
    }

    void Simulation::SetSpeedIndex(i32 index)
    {
        const i32 clamped = std::clamp(index, 0, static_cast<i32>(m_speedSteps.size()) - 1);
        if (clamped > 0) m_previousSpeedIndex = clamped;
        m_speedIndex = clamped;
    }

    void Simulation::TogglePause()
    {
        SetSpeedIndex(IsPaused() ? m_previousSpeedIndex : 0);
    }

    void Simulation::RequestCoverageRefresh()
    {
        CoverageSystem::Get().MarkDirty();
    }

    void Simulation::Update(World& world, f32 realSeconds)
    {
        UpdatePresentation(world, realSeconds);

        // A single party runs every tick the clock allows. A stall cannot fast-forward the
        // world by more than a few days in one frame.
        const i32 limit = std::max(1, static_cast<i32>(4.0f / m_tickDays));
        const i32 due = TakeDueTicks(realSeconds, limit);
        for (i32 i = 0; i < due; ++i) Step(world);
    }

    void Simulation::UpdatePresentation(World& world, f32 realSeconds)
    {
        // What the realm can see is refreshed before the borders are, so a newly scouted
        // frontier is committed to memory in the same frame it is first laid eyes on.
        WOC_PROFILE("sim.frame");
        FogSystem& fog = FogSystem::Get();
        fog.Update(world, realSeconds);
        Renderer::Get().SetFogEnabled(fog.IsEnabled());
        if (fog.IsEnabled())
        {
            const u32 tint = static_cast<u32>(std::strtoul(
                ConfigManager::Get().Str("render/fog/color", "141b26").c_str(), nullptr, 16));
            Renderer::Get().SetClearColor(Color::FromRGB(tint));
        }
        if (fog.ConsumeDirty())
        {
            WOC_PROFILE("coverage.refreshLayer");
            CoverageSystem::Get().RefreshLayer(world);
        }

        // Alone, the border flood runs on a worker and lands when it lands. In a shared party
        // that would land at a different moment on each machine, so there it is done inside
        // the tick instead (see Step) and never here.
        if (!m_lockstep)
        {
            { WOC_PROFILE("coverage.apply"); CoverageSystem::Get().Update(world); }
            if (CoverageSystem::Get().IsDirty())
            {
                WOC_PROFILE("coverage.dispatch");
                CoverageSystem::Get().Recompute(world);
            }
        }
        ForestrySystem::Get().UploadLayers(world);
        { WOC_PROFILE("roads.upload"); RoadSystem::Get().UploadLayer(world); }
    }

    i32 Simulation::TakeDueTicks(f32 realSeconds, i32 limit)
    {
        const f32 speed = SpeedMultiplier();
        if (speed <= 0.0f) return 0;

        m_carryDays += realSeconds * m_daysPerSecond * speed;
        i32 due = static_cast<i32>(m_carryDays / m_tickDays);
        if (due > limit)
        {
            due = limit;
            m_carryDays = 0.0f;          // a stall is not made up for
        }
        else
        {
            m_carryDays -= static_cast<f32>(due) * m_tickDays;
        }
        return due;
    }

    void Simulation::Step(World& world)
    {
        const f32 days = m_tickDays;
        ++m_tick;

        // Borders the last tick invalidated are settled before anything reads them.
        if (m_lockstep && CoverageSystem::Get().IsDirty())
        {
            WOC_PROFILE("sim.coverage");
            CoverageSystem::Get().RecomputeBlocking(world);
        }

        { WOC_PROFILE("sim.movement"); MovementSystem::Get().Tick(world, days); }
        { WOC_PROFILE("sim.battle");   BattleSystem::Get().Tick(world, days); }
        { WOC_PROFILE("sim.roads");    RoadSystem::Get().Tick(world, days); }
        // Saplings grow by the day like everything else that is being built, rather than
        // waiting for the month's forestry pass.
        { WOC_PROFILE("sim.planting"); ForestrySystem::Get().TickPlantations(world, days); }
        { WOC_PROFILE("sim.bandits");  BanditSystem::Get().Tick(world, days); }
        { WOC_PROFILE("sim.musters");  SettlementSystem::Get().TickMusters(world, days); }

        const i32 elapsed = world.Time().Advance(days);
        if (elapsed <= 0) return;

        { WOC_PROFILE("sim.daily");    RunDailySystems(world, elapsed); }
        { WOC_PROFILE("sim.periodic"); RunPeriodicSystems(world, world.Time().TotalDays()); }
    }

    namespace
    {
        struct Fnv
        {
            u64 value = 1469598103934665603ull;
            void Bytes(const void* data, size_t size)
            {
                const auto* bytes = static_cast<const u8*>(data);
                for (size_t i = 0; i < size; ++i) { value ^= bytes[i]; value *= 1099511628211ull; }
            }
            template <typename T> void Add(const T& v) { Bytes(&v, sizeof(T)); }
        };
    }

    u64 Simulation::Checksum(const World& world) const
    {
        // The things a desynchronisation shows up in soonest: where every host stands and how
        // many are in it, what every town holds, what every house has in the treasury. Walked
        // in id order, so the fingerprint does not depend on how a container happens to be
        // laid out in memory.
        Fnv fnv;
        fnv.Add(m_tick);
        fnv.Add(world.Time().TotalDays());

        std::vector<EntityId> ids;
        ids.reserve(world.Cohorts().size());
        for (const auto& [id, cohort] : world.Cohorts()) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        for (EntityId id : ids)
        {
            const Cohort* cohort = world.FindCohort(id);
            fnv.Add(id);
            fnv.Add(cohort->position.x);
            fnv.Add(cohort->position.y);
            fnv.Add(cohort->organisation);
            fnv.Add(static_cast<u32>(cohort->units.size()));
            fnv.Add(static_cast<u8>(cohort->currentTask.type));
        }

        ids.clear();
        for (const auto& [id, settlement] : world.Settlements()) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        for (EntityId id : ids)
        {
            const Settlement* settlement = world.FindSettlement(id);
            fnv.Add(id);
            fnv.Add(settlement->owner);
            fnv.Add(settlement->population);
            fnv.Add(settlement->loyalty);
            fnv.Add(settlement->prosperity);
        }

        ids.clear();
        for (const auto& [id, clan] : world.Clans()) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        for (EntityId id : ids)
        {
            const Clan* clan = world.FindClan(id);
            fnv.Add(id);
            fnv.Add(clan->resources.money);
            fnv.Add(clan->resources.wood);
            fnv.Add(clan->resources.stone);
            fnv.Add(clan->resources.food);
        }
        return fnv.value;
    }

    Json Simulation::ToJson() const
    {
        Json node = Json::MakeObject();
        node["tick"] = static_cast<i64>(m_tick);
        node["economy"] = m_lastEconomyDay;
        node["population"] = m_lastPopulationDay;
        node["forestry"] = m_lastForestryDay;
        node["ai"] = m_lastAIDay;
        node["coverage"] = m_lastCoverageDay;
        node["year"] = m_lastYearDay;
        return node;
    }

    void Simulation::FromJson(const Json& node)
    {
        m_tick = static_cast<u64>(node["tick"].AsNumber(0.0));
        m_lastEconomyDay = node["economy"].AsInt(0);
        m_lastPopulationDay = node["population"].AsInt(0);
        m_lastForestryDay = node["forestry"].AsInt(0);
        m_lastAIDay = node["ai"].AsInt(0);
        m_lastCoverageDay = node["coverage"].AsInt(0);
        m_lastYearDay = node["year"].AsInt(0);
        m_carryDays = 0.0f;
    }

    void Simulation::RunDailySystems(World& world, i32 days)
    {
        { WOC_PROFILE("daily.settlements"); SettlementSystem::Get().Tick(world, days); }
        { WOC_PROFILE("daily.contacts"); DiplomacySystem::Get().UpdateContacts(world); }

        // Hosts standing by for trouble at home look for it once a day, which is often
        // enough for a rising and rare enough to cost nothing.
        { WOC_PROFILE("daily.revolts"); MovementSystem::Get().AnswerRevolts(world); }

        {
            WOC_PROFILE("daily.players");
            for (const Scope<IPlayer>& player : world.Players())
            {
                player->OnDay(world, world.Time().TotalDays());
            }
        }
    }

    void Simulation::RunPeriodicSystems(World& world, i32 today)
    {
        if (today - m_lastEconomyDay >= m_economyTickDays)
        {
            const i32 elapsed = today - m_lastEconomyDay;
            m_lastEconomyDay = today;
            // Prices are set before the treasury is settled, so what a realm earns this
            // fortnight is already worth what this fortnight says it is worth.
            WOC_PROFILE("periodic.economy");
            MarketSystem::Get().Tick(world, elapsed);
            EconomySystem::Get().Tick(world, elapsed);
        }

        if (today - m_lastPopulationDay >= m_populationTickDays)
        {
            m_lastPopulationDay = today;
            PopulationSystem::Get().Tick(world);
            DiplomacySystem::Get().Tick(world);
            PoliticsSystem::Get().Tick(world);
        }

        if (today - m_lastForestryDay >= m_forestryTickDays)
        {
            m_lastForestryDay = today;
            ForestrySystem::Get().Tick(world);
        }

        if (today - m_lastAIDay >= m_aiTickDays)
        {
            m_lastAIDay = today;
            for (const Scope<IPlayer>& player : world.Players())
            {
                if (!player->IsHuman()) player->OnThink(world, today);
            }
        }

        if (today - m_lastCoverageDay >= m_coverageTickDays)
        {
            m_lastCoverageDay = today;
            if (m_lockstep) CoverageSystem::Get().RecomputeBlocking(world);
            else CoverageSystem::Get().Recompute(world);
        }

        if (today - m_lastYearDay >= m_daysPerYear)
        {
            m_lastYearDay = today;
            DynastySystem::Get().Tick(world);
        }
    }
}
