// PoliticsSystem.h - who is still in the game.
//
// A house that holds no land is finished; a realm whose houses are all finished is off the
// map. Once only one realm is left standing, the party is over one way or the other.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Types.h"

namespace woc
{
    class World;

    enum class Outcome { Playing, Victory, Defeat };

    class PoliticsSystem final : public Singleton<PoliticsSystem>
    {
        friend class Singleton<PoliticsSystem>;
    public:
        void Reset() { m_outcome = Outcome::Playing; m_reason.clear(); }

        /// Retires landless houses and realms, then judges the party.
        void Tick(World& world);

        Outcome CurrentOutcome() const { return m_outcome; }
        const std::string& Reason() const { return m_reason; }

    private:
        PoliticsSystem() = default;
        ~PoliticsSystem() = default;

        void RetireLandlessClans(World& world);
        void RetireEmptyStates(World& world);
        void JudgeParty(World& world);

        Outcome m_outcome = Outcome::Playing;
        std::string m_reason;
    };
}
