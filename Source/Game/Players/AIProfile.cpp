#include "AIProfile.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        AIStance StanceFromId(const std::string& id)
        {
            if (id == "defensive") return AIStance::Defensive;
            if (id == "aggressive") return AIStance::Aggressive;
            return AIStance::Cautious;
        }

        std::vector<AIProfile> BuiltIn()
        {
            // Only reached when game.json has no ai/profiles block at all.
            AIProfile defensive;
            defensive.stance = AIStance::Defensive;
            defensive.id = "defensive";
            defensive.name = "Оборонний";
            defensive.defenceWeight = 1.9f;
            defensive.expansionWeight = 0.6f;
            defensive.minGarrisonUnits = 4;
            defensive.expansionIntervalDays = 260;
            defensive.requiredEdge = 1.8f;
            defensive.reachBeyondBorder = 90.0f;
            defensive.homeGarrisonShare = 0.75f;
            defensive.allowRaiding = false;
            defensive.refillBelow = 0.8f;
            defensive.refillUntil = 0.95f;
            defensive.warAppetite = 0.04f;
            defensive.warStrengthRatio = 2.2f;

            AIProfile cautious;   // the defaults in the header already describe this one

            AIProfile aggressive;
            aggressive.stance = AIStance::Aggressive;
            aggressive.id = "aggressive";
            aggressive.name = "Войовничий";
            aggressive.economyWeight = 0.7f;
            aggressive.defenceWeight = 0.8f;
            aggressive.expansionWeight = 1.4f;
            aggressive.minGarrisonUnits = 1;
            aggressive.expansionIntervalDays = 130;
            aggressive.requiredEdge = 0.95f;
            aggressive.reachBeyondBorder = 620.0f;
            aggressive.homeGarrisonShare = 0.25f;
            aggressive.allowRaiding = true;
            aggressive.refillBelow = 0.4f;
            aggressive.refillUntil = 0.7f;
            aggressive.warAppetite = 0.45f;
            aggressive.warStrengthRatio = 1.05f;

            return { defensive, cautious, aggressive };
        }
    }

    AIProfile AIProfile::FromJson(const Json& node)
    {
        AIProfile profile;
        profile.id = node["id"].AsString(profile.id);
        profile.stance = StanceFromId(profile.id);
        profile.name = node["name"].AsString(profile.name);

        profile.economyWeight = node["economyWeight"].AsFloat(profile.economyWeight);
        profile.defenceWeight = node["defenceWeight"].AsFloat(profile.defenceWeight);
        profile.expansionWeight = node["expansionWeight"].AsFloat(profile.expansionWeight);
        profile.minGarrisonUnits = node["minGarrisonUnits"].AsInt(profile.minGarrisonUnits);
        profile.expansionIntervalDays = node["expansionIntervalDays"].AsInt(profile.expansionIntervalDays);

        profile.requiredEdge = node["requiredEdge"].AsFloat(profile.requiredEdge);
        profile.reachBeyondBorder = node["reachBeyondBorder"].AsFloat(profile.reachBeyondBorder);
        profile.homeGarrisonShare = node["homeGarrisonShare"].AsFloat(profile.homeGarrisonShare);
        profile.allowRaiding = node["allowRaiding"].AsBool(profile.allowRaiding);
        profile.refillBelow = node["refillBelow"].AsFloat(profile.refillBelow);
        profile.refillUntil = std::max(profile.refillBelow, node["refillUntil"].AsFloat(profile.refillUntil));

        profile.warAppetite = node["warAppetite"].AsFloat(profile.warAppetite);
        profile.warStrengthRatio = node["warStrengthRatio"].AsFloat(profile.warStrengthRatio);
        return profile;
    }

    const std::vector<AIProfile>& AIProfile::All()
    {
        static std::vector<AIProfile> profiles;
        static bool loaded = false;
        if (loaded) return profiles;
        loaded = true;

        const Json& node = ConfigManager::Get().Game().Get("ai/profiles");
        if (node.IsArray() && node.Size() > 0)
        {
            for (size_t i = 0; i < node.Size(); ++i) profiles.push_back(FromJson(node[i]));
        }
        else
        {
            profiles = BuiltIn();
            WOC_LOG_WARN("ai/profiles missing from game.json; using the built-in temperaments");
        }
        return profiles;
    }

    const AIProfile& AIProfile::Pick(Random& random)
    {
        const std::vector<AIProfile>& profiles = All();
        const i32 index = random.Range(0, static_cast<i32>(profiles.size()) - 1);
        return profiles[static_cast<size_t>(index)];
    }

    const char* AIProfile::StanceName(AIStance stance)
    {
        switch (stance)
        {
        case AIStance::Defensive:  return "Оборонний";
        case AIStance::Cautious:   return "Обережний";
        case AIStance::Aggressive: return "Войовничий";
        }
        return "?";
    }
}
