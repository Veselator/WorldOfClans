#include "PoliticsSystem.h"
#include "CoverageSystem.h"
#include "../World/World.h"

#include <algorithm>

namespace woc
{
    void PoliticsSystem::Tick(World& world)
    {
        if (m_outcome != Outcome::Playing) return;

        RetireLandlessClans(world);
        RetireEmptyStates(world);
        JudgeParty(world);
    }

    void PoliticsSystem::RetireLandlessClans(World& world)
    {
        std::vector<EntityId> finished;
        for (const auto& [id, clan] : world.Clans())
        {
            if (clan.eliminated) continue;
            if (!clan.settlements.empty()) continue;
            finished.push_back(id);
        }

        for (EntityId id : finished)
        {
            Clan* clan = world.FindClan(id);
            if (!clan) continue;

            // Its remaining armies disperse: without land there is nothing to pay them with.
            const std::vector<EntityId> armies = clan->cohorts;
            for (EntityId cohortId : armies) world.DestroyCohort(cohortId);

            clan->eliminated = true;
            if (State* state = world.FindState(clan->state)) state->RemoveClan(id);

            world.Log("Рід " + clan->name + " втратив усі землі й зійшов зі сцени",
                      Color::FromRGB(0x8B97A4));
            CoverageSystem::Get().MarkDirty();
        }
    }

    void PoliticsSystem::RetireEmptyStates(World& world)
    {
        std::vector<EntityId> finished;
        for (const auto& [id, state] : world.States())
        {
            if (state.eliminated) continue;

            bool anyClanLeft = false;
            for (EntityId clanId : state.clans)
            {
                const Clan* clan = world.FindClan(clanId);
                if (clan && !clan->eliminated) { anyClanLeft = true; break; }
            }
            if (!anyClanLeft) finished.push_back(id);
        }

        for (EntityId id : finished)
        {
            State* state = world.FindState(id);
            if (!state) continue;
            state->eliminated = true;
            world.Log("Держава " + state->name + " припинила існування", Color::FromRGB(0xC05046));
        }
    }

    void PoliticsSystem::JudgeParty(World& world)
    {
        const State* human = const_cast<World&>(world).HumanState();
        if (!human) return;

        // Defeat: no land left anywhere in the realm.
        size_t ownLands = 0;
        for (EntityId clanId : human->clans)
        {
            if (const Clan* clan = world.FindClan(clanId)) ownLands += clan->settlements.size();
        }

        if (human->eliminated || ownLands == 0)
        {
            m_outcome = Outcome::Defeat;
            m_reason = "Ваша держава втратила всі землі.";
            world.Log("Поразка: " + human->name + " більше не володіє нічим", Color::FromRGB(0xC05046));
            return;
        }

        // Victory: nobody else is left standing.
        for (const auto& [id, state] : world.States())
        {
            if (id == human->id || state.eliminated) continue;

            bool holdsLand = false;
            for (EntityId clanId : state.clans)
            {
                const Clan* clan = world.FindClan(clanId);
                if (clan && !clan->eliminated && !clan->settlements.empty()) { holdsLand = true; break; }
            }
            if (holdsLand) return;   // a rival still stands
        }

        m_outcome = Outcome::Victory;
        m_reason = "Жодного суперника не лишилося — уся земля ваша.";
        world.Log("Перемога: " + human->name + " володіє всією землею", Color::FromRGB(0xC9A227));
    }
}
