// AIProfile.h - the temperament of an artificial lord.
//
// The AI has no single "difficulty": it has a character. A defensive prince sits on his
// walls and lets the countryside come to him; an aggressive one campaigns past his own
// frontier; a cautious one only moves when the odds are plainly his. All three run the
// same code - only these numbers differ, and all of them live in game.json.
#pragma once

#include "../../Core/Json.h"
#include "../../Core/Random.h"
#include "../../Core/Types.h"

#include <string>
#include <vector>

namespace woc
{
    enum class AIStance : u8
    {
        Defensive,
        Cautious,
        Aggressive
    };

    struct AIProfile
    {
        AIStance stance = AIStance::Cautious;
        std::string id = "cautious";
        std::string name = "Обережний";

        // --- what it spends on ---------------------------------------------------------
        f32 economyWeight = 0.9f;
        f32 defenceWeight = 1.2f;
        f32 expansionWeight = 1.0f;
        i32 minGarrisonUnits = 2;
        i32 expansionIntervalDays = 180;

        // --- how it fights -------------------------------------------------------------
        /// Power-to-defence ratio it insists on before laying siege.
        f32 requiredEdge = 1.3f;
        /// How far past its own frontier it will campaign, in map units. This, not a flat
        /// map-wide limit, is what keeps an army from wandering to the far coast.
        f32 reachBeyondBorder = 260.0f;
        /// Share of its armies held back on home soil.
        f32 homeGarrisonShare = 0.5f;
        /// Below this share of its full muster a host is taken home to be filled up; it
        /// stays there until it is back to refillUntil. A careful lord pulls out early, a
        /// reckless one fights on with half a company.
        f32 refillBelow = 0.65f;
        f32 refillUntil = 0.9f;
        bool allowRaiding = true;

        // --- how it talks --------------------------------------------------------------
        /// Chance per think of declaring war on a weaker neighbour.
        f32 warAppetite = 0.15f;
        /// Relative strength it wants before it declares one.
        f32 warStrengthRatio = 1.4f;

        static AIProfile FromJson(const Json& node);
        /// Every profile in game.json, in declaration order. Falls back to three built-ins.
        static const std::vector<AIProfile>& All();
        /// One profile at random, for a freshly seated AI lord.
        static const AIProfile& Pick(Random& random);
        static const char* StanceName(AIStance stance);
    };
}
