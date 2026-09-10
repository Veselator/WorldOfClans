// UnitDatabase.h - everything units.json describes, in memory.
//
// Roles, per-race stat blocks, the rock-paper-scissors matrix, terrain affinities and
// the recruitment rules all come from that one document, exactly as specified.
#pragma once

#include "../../Core/Singleton.h"
#include "../../Core/Math.h"
#include "Character.h"
#include <unordered_map>

namespace woc
{
    enum class UnitRole : u8
    {
        Aristocrat = 0,
        Swordsman,
        Archer,
        Cavalry,
        HorseArcher,
        Count
    };

    struct RoleInfo
    {
        UnitRole role = UnitRole::Swordsman;
        std::string id;
        std::string name;
        std::string description;
        bool single = false;      // aristocrats are always a company of one
    };

    /// The stat block a unit is built from - "UnitData" in the design sketch.
    struct UnitData
    {
        UnitRole role = UnitRole::Swordsman;
        std::string roleId;
        std::string raceId;
        std::string name;
        f32 attack = 10.0f;
        f32 defense = 10.0f;
        f32 health = 20.0f;
        f32 speed = 1.0f;
        f32 range = 1.0f;
        f32 recruitCost = 20.0f;
        f32 upkeep = 0.15f;
        f32 morale = 0.75f;
        f32 commandBonus = 0.0f;
    };

    class UnitDatabase final : public Singleton<UnitDatabase>
    {
        friend class Singleton<UnitDatabase>;
    public:
        void Load();

        const std::vector<RoleInfo>& Roles() const { return m_roles; }
        const RoleInfo& Role(UnitRole role) const;
        static const char* RoleId(UnitRole role);
        static UnitRole ParseRole(const std::string& id);

        /// Stat block for a race/role pair; falls back to the first human unit.
        const UnitData& Stats(const std::string& raceId, UnitRole role) const;
        bool HasRace(const std::string& raceId) const;

        /// Damage multiplier when `attacker` engages `defender`.
        f32 Counter(UnitRole attacker, UnitRole defender) const;
        /// Combat multiplier for a role on a given terrain id.
        f32 TerrainAffinity(UnitRole role, const std::string& terrainId) const;

        const Trait* FindTrait(const std::string& id) const;
        const std::vector<Trait>& Traits() const { return m_traits; }

        u32 UnitSizeForTier(const std::string& tierKey) const;
        u32 MaxUnitsPerCohort() const { return m_maxUnits; }
        u32 MaxCharactersPerUnit() const { return m_maxCharacters; }
        f32 BaseMorale() const { return m_baseMorale; }
        f32 BaseTraining() const { return m_baseTraining; }
        f32 TrainingPerMonth() const { return m_trainingPerMonth; }
        f32 ExperiencePerBattle() const { return m_experiencePerBattle; }
        f32 ExperienceCombatWeight() const { return m_experienceCombatWeight; }
        f32 MinLoyaltyToRecruit() const { return m_minLoyaltyToRecruit; }
        f32 LoyaltyCostPerUnit() const { return m_loyaltyCostPerUnit; }

        Vec2 HeightRange(const std::string& raceId) const;
        f32 WeightFactor(const std::string& raceId) const;
        Vec2 AgeRange() const { return m_ageRange; }

    private:
        UnitDatabase() = default;
        ~UnitDatabase() = default;

        std::vector<RoleInfo> m_roles;
        std::unordered_map<std::string, std::vector<UnitData>> m_unitsByRace;
        std::unordered_map<std::string, std::unordered_map<std::string, f32>> m_counters;
        std::unordered_map<std::string, std::unordered_map<std::string, f32>> m_terrainAffinity;
        std::unordered_map<std::string, u32> m_unitSizeByTier;
        std::unordered_map<std::string, Vec2> m_heightRanges;
        std::unordered_map<std::string, f32> m_weightFactors;
        std::vector<Trait> m_traits;

        u32 m_maxUnits = 10;
        u32 m_maxCharacters = 100;
        f32 m_baseMorale = 0.75f;
        f32 m_baseTraining = 0.35f;
        f32 m_trainingPerMonth = 0.02f;
        f32 m_experiencePerBattle = 0.02f;
        f32 m_experienceCombatWeight = 0.35f;
        f32 m_minLoyaltyToRecruit = 0.2f;
        f32 m_loyaltyCostPerUnit = 0.01f;
        Vec2 m_ageRange{ 16.0f, 52.0f };
    };
}
