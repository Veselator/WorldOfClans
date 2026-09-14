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
        Storm,          // burn out a robbers' camp: there is nothing there to besiege
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

        /// How well the host is held together: ranks dressed, orders passing, baggage up.
        /// A battle or a forced march tears it; standing still on friendly soil mends it,
        /// and the bigger the host the slower it mends, because there is more of it to
        /// put back in order. Combat strength leans on this as hard as it does on numbers.
        f32 organisation = 1.0f;
        bool inBattle = false;
        /// Whether this army will plunder enemy holdings. Off by default: burning the
        /// countryside is a deliberate choice, not something a march does on its own.
        bool mayRaid = false;
        /// Whether this host answers risings inside the realm on its own. Off by default:
        /// a prince's field army should not wander off to a burning village unless he says
        /// so. Turned on, it marches at the nearest revolt in the realm's own borders.
        bool suppressRevolts = false;
        /// Days left of breaking contact. A host that has been ordered to pull back is not
        /// dragged into another round while it is going; it also cannot be given a fresh
        /// fight until it has caught its breath.
        f32 disengageDays = 0.0f;
        bool IsWithdrawing() const { return disengageDays > 0.0f; }
        /// Falling back from a lost fight or a broken-off one: the host moves at a run until
        /// it reaches the ground it was making for, or until it is given a new order.
        bool retreating = false;
        EntityId garrisonOf = kInvalidId;   // settlement this cohort is stationed in
        /// The robbers' camp this band came out of, and goes back to when it is mauled.
        /// kInvalidId for everybody else, and for a band whose camp has been burnt.
        EntityId homeCamp = kInvalidId;
        /// The walls this host sat down in front of. Kept apart from the task, because a
        /// relief army arriving clears the task and the siege would otherwise be forgotten
        /// the moment the field battle began.
        EntityId siegeTarget = kInvalidId;

        Task currentTask;

        bool IsEmpty() const { return units.empty(); }

        std::string DisplayName() const { return name.empty() ? "Когорта" : name; }

        Json ToJson() const;
        static Cohort FromJson(const Json& node);
    };
}
