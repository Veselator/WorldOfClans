// Simulation.h - drives the clock and calls the systems in the right order.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"
#include "../../Core/Json.h"

namespace woc
{
    class World;

    class Simulation final : public Singleton<Simulation>
    {
        friend class Singleton<Simulation>;
    public:
        void Configure();
        void Reset();

        /// Advances the world by `realSeconds` of wall time at the current speed: the
        /// presentation every frame, and as many whole simulation ticks as are due.
        void Update(World& world, f32 realSeconds);

        // --- the fixed step ------------------------------------------------------------------
        // The world moves in ticks of a fixed length of game time and nothing else. The same
        // world given the same orders at the same ticks comes out the same on every machine,
        // which is what a multiplayer party is built on: only the orders cross the wire.

        /// Everything that is this machine's alone - fog, borders on screen, layer uploads.
        /// Never changes the world.
        void UpdatePresentation(World& world, f32 realSeconds);
        /// How many ticks the wall clock has made due since the last call, at this speed.
        /// Consumes them; the caller decides whether it may run them.
        i32 TakeDueTicks(f32 realSeconds, i32 limit);
        /// One tick of the world. The only function that advances the simulation.
        void Step(World& world);
        u64 CurrentTick() const { return m_tick; }
        f32 TickDays() const { return m_tickDays; }
        /// A fingerprint of the world, for two machines to compare. Equal worlds give equal
        /// fingerprints; a difference means a desynchronisation.
        u64 Checksum(const World& world) const;

        /// In a multiplayer party every step must happen at the same point on every machine,
        /// so work that would otherwise finish "whenever the worker is done" - the border
        /// flood - is done inside the tick instead.
        void SetLockstep(bool lockstep) { m_lockstep = lockstep; }
        bool IsLockstep() const { return m_lockstep; }

        /// The clock's own bookkeeping, for a resynchronisation to carry across.
        Json ToJson() const;
        void FromJson(const Json& node);

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
        bool m_lockstep = false;
        u64 m_tick = 0;
        f32 m_tickDays = 1.0f / 24.0f;
        f32 m_carryDays = 0.0f;

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
