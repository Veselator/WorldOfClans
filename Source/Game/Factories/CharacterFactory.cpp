#include "CharacterFactory.h"
#include "NamePool.h"
#include "../World/World.h"
#include "../World/UnitDatabase.h"
#include "../../Core/Config.h"

#include <algorithm>

namespace woc
{
    void CharacterFactory::RollPhysique(Character& character, Random& random)
    {
        const UnitDatabase& db = UnitDatabase::Get();
        const Vec2 range = db.HeightRange(character.raceId);

        // Height is normally distributed inside the racial range rather than uniform,
        // so most people are ordinary and the extremes are rare.
        const f32 mean = (range.x + range.y) * 0.5f;
        const f32 deviation = (range.y - range.x) / 6.0f;
        character.height = std::clamp(random.Gaussian(mean, deviation), range.x, range.y);

        // Build varies around a race-typical mass-for-height factor.
        const f32 factor = db.WeightFactor(character.raceId) * random.RangeF(0.9f, 1.12f);
        character.weight = character.height * factor;

        if (character.gender == Gender::Female)
        {
            character.height *= 0.94f;
            character.weight *= 0.86f;
        }
    }

    void CharacterFactory::RollTraits(Character& character, Random& random, int maximum)
    {
        const std::vector<Trait>& traits = UnitDatabase::Get().Traits();
        if (traits.empty() || maximum <= 0) return;

        const int count = random.Range(0, maximum);
        for (int i = 0; i < count; ++i)
        {
            const Trait& trait = traits[static_cast<size_t>(random.Range(0, static_cast<i32>(traits.size()) - 1))];
            if (std::find(character.traits.begin(), character.traits.end(), trait.id) == character.traits.end())
            {
                character.traits.push_back(trait.id);
            }
        }
    }

    Character& CharacterFactory::CreateCommoner(World& world, const std::string& raceId,
                                                const std::string& origin, Random& random)
    {
        const NamePool& names = NamePool::Get();
        const Vec2 ageRange = UnitDatabase::Get().AgeRange();

        Character& character = world.CreateCharacter();
        character.raceId = raceId;
        character.gender = Gender::Male;   // levies of the age were men
        character.givenName = names.GivenName(raceId, false, random);
        character.surname = random.Chance(0.55f) ? names.Surname(raceId, random) : std::string();
        character.origin = origin;
        character.age = random.Range(static_cast<i32>(ageRange.x), static_cast<i32>(ageRange.y));
        character.noble = false;
        RollPhysique(character, random);
        RollTraits(character, random, 1);
        return character;
    }

    Character& CharacterFactory::CreateNoble(World& world, const std::string& raceId, EntityId clanId,
                                             const std::string& origin, Gender gender, i32 age, Random& random)
    {
        const NamePool& names = NamePool::Get();

        Character& character = world.CreateCharacter();
        character.raceId = raceId;
        character.gender = gender;
        character.givenName = names.GivenName(raceId, gender == Gender::Female, random);
        character.surname = names.Surname(raceId, random);
        character.origin = origin;
        character.age = age;
        character.noble = true;
        character.clan = clanId;
        character.birthDay = -age * ConfigManager::Get().Int("simulation/daysPerMonth", 30) *
                                    ConfigManager::Get().Int("simulation/monthsPerYear", 12);
        RollPhysique(character, random);
        RollTraits(character, random, 2);

        if (Clan* clan = world.FindClan(clanId))
        {
            clan->members.push_back(character.id);
        }
        return character;
    }

    Character& CharacterFactory::CreateChild(World& world, Character& father, Character& mother, Random& random)
    {
        const NamePool& names = NamePool::Get();
        const Gender gender = random.Chance(0.5f) ? Gender::Male : Gender::Female;

        Character& child = world.CreateCharacter();
        child.raceId = father.raceId;
        child.gender = gender;
        child.givenName = names.GivenName(child.raceId, gender == Gender::Female, random);
        child.surname = father.surname;      // the house name passes down the male line
        child.origin = father.origin;
        child.age = 0;
        child.noble = true;
        child.clan = father.clan;
        child.father = father.id;
        child.mother = mother.id;
        child.birthDay = world.Time().TotalDays();
        RollPhysique(child, random);
        RollTraits(child, random, 1);

        father.children.push_back(child.id);
        mother.children.push_back(child.id);
        if (Clan* clan = world.FindClan(child.clan)) clan->members.push_back(child.id);
        return child;
    }
}
