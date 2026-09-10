// Cohort.h - an army on the map: up to ten units under one banner and one order.
#pragma once

#include "Unit.h"
#include "../../Core/Math.h"

namespace woc
{
    enum class TaskType : u8
    {
        Idle,
        Move,
        Garrison,       // sit inside a settlement, train and recover
        Attack,         // intercept an enemy cohort
        Besiege,        // stand before a settlement and reduce it
        Raid,           // plunder a settlement without taking it
        Patrol
    };

    /// The single order a cohort is executing. Movement is expressed as a waypoint list
    /// produced by A*, so the order survives being re-issued and re-pathed.
    struct Task
    {
        TaskType type = TaskType::Idle;
        Vec2 destination;
        EntityId targetCohort = kInvalidId;
        EntityId targetSettlement = kInvalidId;
        std::vector<Vec2> waypoints;
        size_t waypointIndex = 0;
        f32 progressDays = 0.0f;

        bool IsMoving() const { return waypointIndex < waypoints.size(); }
        void Clear()
        {
            type = TaskType::Idle;
            waypoints.clear();
            waypointIndex = 0;
            targetCohort = kInvalidId;
            targetSettlement = kInvalidId;
            progressDays = 0.0f;
        }

        static const char* TypeName(TaskType type);
    };

    class Cohort
    {
    public:
        EntityId id = kInvalidId;
        EntityId clan = kInvalidId;

        std::string name;
        Vec2 position;
        std::vector<EntityId> units;

        f32 experience = 0.0f;      // 0..1, grows with every battle
        f32 supply = 1.0f;          // 0..1, drops away from friendly land
        bool inBattle = false;
        EntityId garrisonOf = kInvalidId;   // settlement this cohort is stationed in

        Task currentTask;

        bool IsEmpty() const { return units.empty(); }

        std::string DisplayName() const { return name.empty() ? "Когорта" : name; }

        Json ToJson() const;
        static Cohort FromJson(const Json& node);
    };
}
