#include "UnitFactory.h"
#include "CharacterFactory.h"
#include "NamePool.h"
#include "../World/World.h"

#include <algorithm>

namespace woc
{
    u32 UnitFactory::EstablishmentFor(const Settlement& settlement)
    {
        const SettlementKindInfo& kind = settlement.KindInfo();
        const i32 tier = settlement.Tier().tier;

        static const char* kRomans[] = { "I", "II", "III" };
        const std::string key = kind.id + kRomans[std::clamp(tier - 1, 0, 2)];
        return UnitDatabase::Get().UnitSizeForTier(key);
    }

    Unit& UnitFactory::Create(World& world, EntityId cohortId, const std::string& raceId, UnitRole role,
                              u32 headCount, const std::string& origin, Random& random)
    {
        const UnitDatabase& db = UnitDatabase::Get();
        const RoleInfo& roleInfo = db.Role(role);

        Unit& unit = world.CreateUnit();
        unit.cohort = cohortId;
        unit.role = role;
        unit.raceId = raceId;
        unit.name = db.Stats(raceId, role).name;
        unit.morale = db.BaseMorale();
        unit.training = db.BaseTraining();

        const u32 requested = roleInfo.single ? 1u : std::clamp(headCount, 1u, db.MaxCharactersPerUnit());
        unit.establishment = requested;

        for (u32 i = 0; i < requested; ++i)
        {
            Character& person = CharacterFactory::CreateCommoner(world, raceId, origin, random);
            unit.characters.push_back(person.id);
        }

        if (Cohort* cohort = world.FindCohort(cohortId))
        {
            cohort->units.push_back(unit.id);
        }
        return unit;
    }

    Cohort& UnitFactory::CreateCohort(World& world, EntityId clanId, const Vec2& position, Random& random,
                                      const std::string& name)
    {
        Cohort& cohort = world.CreateCohort();
        cohort.clan = clanId;
        cohort.position = position;
        cohort.name = name.empty() ? NamePool::Get().CohortName(random) : name;
        cohort.experience = 0.0f;
        cohort.currentTask.Clear();

        if (Clan* clan = world.FindClan(clanId)) clan->AddCohort(cohort.id);
        return cohort;
    }

    Cohort& UnitFactory::CreateRetinue(World& world, EntityId clanId, const Settlement& home,
                                       u32 unitCount, Random& random)
    {
        Cohort& cohort = CreateCohort(world, clanId, home.position, random);
        cohort.garrisonOf = home.id;
        cohort.currentTask.type = TaskType::Garrison;
        cohort.currentTask.targetSettlement = home.id;

        const u32 establishment = EstablishmentFor(home);
        const u32 count = std::clamp(unitCount, 1u, UnitDatabase::Get().MaxUnitsPerCohort());

        // Every retinue is led by an aristocrat; the rest is a plausible mix of the arms.
        Create(world, cohort.id, home.raceId, UnitRole::Aristocrat, 1, home.name, random);

        static const UnitRole kLine[] = {
            UnitRole::Swordsman, UnitRole::Archer, UnitRole::Swordsman,
            UnitRole::Cavalry, UnitRole::Archer, UnitRole::HorseArcher,
            UnitRole::Swordsman, UnitRole::Cavalry, UnitRole::Archer
        };
        for (u32 i = 1; i < count; ++i)
        {
            const UnitRole role = kLine[(i - 1) % std::size(kLine)];
            const u32 strength = static_cast<u32>(establishment * random.RangeF(0.7f, 1.0f));
            Create(world, cohort.id, home.raceId, role, std::max(8u, strength), home.name, random);
        }
        return cohort;
    }
}
