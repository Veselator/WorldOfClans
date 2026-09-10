#include "Simulation.h"

#include "BattleSystem.h"
#include "CoverageSystem.h"
#include "DiplomacySystem.h"
#include "DynastySystem.h"
#include "EconomySystem.h"
#include "ForestrySystem.h"
#include "MovementSystem.h"
#include "PoliticsSystem.h"
#include "PopulationSystem.h"
#include "SettlementSystem.h"
#include "../Players/IPlayer.h"
#include "../World/World.h"
#include "../../Core/Config.h"

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
        // Coverage is refreshed even while paused so the borders react to a command at once.
        if (CoverageSystem::Get().IsDirty())
        {
            CoverageSystem::Get().Recompute(world);
        }
        ForestrySystem::Get().UploadLayers(world);

        const f32 speed = SpeedMultiplier();
        if (speed <= 0.0f) return;

        // Cap the step so a stall in the renderer cannot fast-forward the world.
        const f32 days = std::min(realSeconds * m_daysPerSecond * speed, 4.0f);

        // Continuous systems move on fractional days; the rest wait for whole days.
        MovementSystem::Get().Tick(world, days);
        BattleSystem::Get().Tick(world, days);

        const i32 elapsed = world.Time().Advance(days);
        if (elapsed <= 0) return;

        RunDailySystems(world, elapsed);
        RunPeriodicSystems(world, world.Time().TotalDays());
    }

    void Simulation::RunDailySystems(World& world, i32 days)
    {
        SettlementSystem::Get().Tick(world, days);

        for (const Scope<IPlayer>& player : world.Players())
        {
            player->OnDay(world, world.Time().TotalDays());
        }
    }

    void Simulation::RunPeriodicSystems(World& world, i32 today)
    {
        if (today - m_lastEconomyDay >= m_economyTickDays)
        {
            m_lastEconomyDay = today;
            EconomySystem::Get().Tick(world);
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
            CoverageSystem::Get().Recompute(world);
        }

        if (today - m_lastYearDay >= m_daysPerYear)
        {
            m_lastYearDay = today;
            DynastySystem::Get().Tick(world);
        }
    }
}
