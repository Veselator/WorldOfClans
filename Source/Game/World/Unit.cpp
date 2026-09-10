#include "Unit.h"

namespace woc
{
    namespace
    {
        /// Experience, morale and training all scale the raw stat line; this is the
        /// single place that combination is defined so attack and defence stay symmetric.
        f32 QualityFactor(f32 experience, f32 morale, f32 training)
        {
            const UnitDatabase& db = UnitDatabase::Get();
            const f32 experienceTerm = 1.0f + Clamp01(experience) * db.ExperienceCombatWeight();
            const f32 moraleTerm = 0.55f + Clamp01(morale) * 0.65f;
            const f32 trainingTerm = 0.75f + Clamp01(training) * 0.5f;
            return experienceTerm * moraleTerm * trainingTerm;
        }
    }

    f32 Unit::CombatPower(f32 cohortExperience) const
    {
        const UnitData& data = Stats();
        const f32 fatigueTerm = 1.0f - Clamp01(fatigue) * 0.3f;
        return data.attack * static_cast<f32>(characters.size()) *
               QualityFactor(cohortExperience, morale, training) * fatigueTerm;
    }

    f32 Unit::DefensivePower(f32 cohortExperience) const
    {
        const UnitData& data = Stats();
        const f32 fatigueTerm = 1.0f - Clamp01(fatigue) * 0.25f;
        return (data.defense + data.health * 0.35f) * static_cast<f32>(characters.size()) *
               QualityFactor(cohortExperience, morale, training) * fatigueTerm;
    }

    std::string Unit::DisplayName() const
    {
        return name.empty() ? Stats().name : name;
    }

    Json Unit::ToJson() const
    {
        Json node = Json::MakeObject();
        node["id"] = EncodeId(id);
        node["cohort"] = EncodeId(cohort);
        node["role"] = UnitDatabase::RoleId(role);
        node["race"] = raceId;
        node["name"] = name;
        node["establishment"] = static_cast<i64>(establishment);
        node["morale"] = morale;
        node["training"] = training;
        node["fatigue"] = fatigue;
        node["characters"] = EncodeIdList(characters);
        return node;
    }

    Unit Unit::FromJson(const Json& node)
    {
        Unit unit;
        unit.id = DecodeId(node["id"]);
        unit.cohort = DecodeId(node["cohort"]);
        unit.role = UnitDatabase::ParseRole(node["role"].AsString("swordsman"));
        unit.raceId = node["race"].AsString("human");
        unit.name = node["name"].AsString();
        unit.establishment = static_cast<u32>(node["establishment"].AsInt(50));
        unit.morale = node["morale"].AsFloat(0.75f);
        unit.training = node["training"].AsFloat(0.35f);
        unit.fatigue = node["fatigue"].AsFloat(0.0f);
        unit.characters = DecodeIdList(node["characters"]);
        return unit;
    }
}
