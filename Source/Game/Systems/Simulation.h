// Simulation.h - drives the clock and calls the systems in the right order.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"

namespace woc
{
    class World;

    class Simulation final : public Singleton<Simulation>
    {
        friend class Singleton<Simulation>;
    public:
        void Configure();
        void Reset();

        /// Advances the world by `realSeconds` of wall time at the current speed.
        void Update(World& world, f32 realSeconds);

        // --- speed control ---------------------------------------------------------------
        void SetSpeedIndex(i32 index);
        i32 SpeedIndex() const { return m_speedIndex; }
        f32 SpeedMultiplier() const;
        const std::vector<f32>& SpeedSteps() const { return m_speedSteps; }
        void TogglePause();
        bool IsPaused() const;

        /// Forces a coverage rebuild on the next update (after a conquest, for instance).
        void RequestCoverageRefresh();

    private:
        Simulation() = default;
        ~Simulation() = default;

        void RunDailySystems(World& world, i32 days);
        void RunPeriodicSystems(World& world, i32 today);

        std::vector<f32> m_speedSteps{ 0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
        i32 m_speedIndex = 2;
        i32 m_previousSpeedIndex = 2;

        f32 m_daysPerSecond = 1.0f;
        i32 m_economyTickDays = 30;
        i32 m_populationTickDays = 30;
        i32 m_forestryTickDays = 30;
        i32 m_aiTickDays = 15;
        i32 m_coverageTickDays = 15;
        i32 m_daysPerYear = 360;

        i32 m_lastEconomyDay = 0;
        i32 m_lastPopulationDay = 0;
        i32 m_lastForestryDay = 0;
        i32 m_lastAIDay = 0;
        i32 m_lastCoverageDay = 0;
        i32 m_lastYearDay = 0;
    };
}
