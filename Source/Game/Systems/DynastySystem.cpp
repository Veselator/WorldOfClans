#include "DynastySystem.h"
#include "../Factories/CharacterFactory.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Random.h"

#include <algorithm>

namespace woc
{
    void DynastySystem::FoundDynasty(World& world, EntityId clanId, const std::string& seatName)
    {
        Clan* clan = world.FindClan(clanId);
        if (!clan) return;

        Random& random = GlobalRandom();
        ConfigManager& config = ConfigManager::Get();
        const i32 marriageAge = config.Int("characters/marriageAge", 18);

        Character& lord = CharacterFactory::CreateNoble(world, clan->raceId, clanId, seatName,
                                                        Gender::Male, random.Range(28, 48), random);
        clan->head = lord.id;

        Character& lady = CharacterFactory::CreateNoble(world, clan->raceId, clanId, seatName,
                                                        Gender::Female, random.Range(marriageAge, 42), random);
        // The whole house shares the lord's surname.
        lady.surname = lord.surname;
        lord.spouse = lady.id;
        lady.spouse = lord.id;

        const int childCount = random.Range(0, 3);
        for (int i = 0; i < childCount; ++i)
        {
            Character& child = CharacterFactory::CreateChild(world, lord, lady, random);
            child.age = random.Range(1, std::max(2, lord.age - marriageAge));
            child.birthDay = -child.age * config.Int("simulation/daysPerMonth", 30) *
                                          config.Int("simulation/monthsPerYear", 12);
        }
    }

    EntityId DynastySystem::HeirOf(const World& world, EntityId clanId) const
    {
        const Clan* clan = world.FindClan(clanId);
        if (!clan) return kInvalidId;

        const Character* head = world.FindCharacter(clan->head);
        const i32 comingOfAge = ConfigManager::Get().Int("characters/comingOfAge", 16);

        // Agnatic primogeniture: eldest living son first, then any child, then any relative.
        auto best = [&](bool malesOnly, bool adultsOnly) -> EntityId
        {
            EntityId chosen = kInvalidId;
            i32 bestAge = -1;
            const std::vector<EntityId>& pool = head ? head->children : clan->members;
            for (EntityId id : pool)
            {
                const Character* candidate = world.FindCharacter(id);
                if (!candidate || !candidate->alive) continue;
                if (malesOnly && candidate->gender != Gender::Male) continue;
                if (adultsOnly && candidate->age < comingOfAge) continue;
                if (candidate->age > bestAge) { bestAge = candidate->age; chosen = id; }
            }
            return chosen;
        };

        EntityId heir = best(true, true);
        if (heir == kInvalidId) heir = best(false, true);
        if (heir == kInvalidId) heir = best(true, false);
        if (heir != kInvalidId) return heir;

        // No direct issue: the eldest adult of the house takes over.
        EntityId eldest = kInvalidId;
        i32 bestAge = -1;
        for (EntityId id : clan->members)
        {
            const Character* candidate = world.FindCharacter(id);
            if (!candidate || !candidate->alive || id == clan->head) continue;
            if (candidate->age < comingOfAge) continue;
            if (candidate->age > bestAge) { bestAge = candidate->age; eldest = id; }
        }
        return eldest;
    }

    std::vector<EntityId> DynastySystem::ImmediateFamily(const World& world, EntityId characterId) const
    {
        std::vector<EntityId> family;
        const Character* character = world.FindCharacter(characterId);
        if (!character) return family;

        if (character->father != kInvalidId) family.push_back(character->father);
        if (character->mother != kInvalidId) family.push_back(character->mother);
        if (character->spouse != kInvalidId) family.push_back(character->spouse);
        family.insert(family.end(), character->children.begin(), character->children.end());
        return family;
    }

    void DynastySystem::Tick(World& world)
    {
        AgeCharacters(world);
        ResolveBirths(world);
        ResolveDeaths(world);
    }

    void DynastySystem::AgeCharacters(World& world)
    {
        for (auto& [id, character] : world.Characters())
        {
            if (character.alive) ++character.age;
        }
    }

    void DynastySystem::ResolveBirths(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        const i32 fertilityStart = config.Int("characters/fertilityStart", 18);
        const i32 fertilityEnd = config.Int("characters/fertilityEnd", 45);
        const f32 chance = config.Float("characters/childChancePerYear", 0.28f);

        Random& random = GlobalRandom();

        // Collect first: creating characters inserts into the same container we scan.
        std::vector<std::pair<EntityId, EntityId>> couples;
        for (const auto& [id, character] : world.Characters())
        {
            if (!character.noble || !character.alive) continue;
            if (character.gender != Gender::Female) continue;
            if (character.spouse == kInvalidId) continue;
            if (character.age < fertilityStart || character.age > fertilityEnd) continue;

            const Character* husband = world.FindCharacter(character.spouse);
            if (!husband || !husband->alive) continue;
            couples.emplace_back(character.spouse, id);
        }

        for (const auto& [fatherId, motherId] : couples)
        {
            if (!random.Chance(chance)) continue;

            Character* father = world.FindCharacter(fatherId);
            Character* mother = world.FindCharacter(motherId);
            if (!father || !mother) continue;

            Character& child = CharacterFactory::CreateChild(world, *father, *mother, random);
            if (Clan* clan = world.FindClan(child.clan))
            {
                world.Log("У роду " + clan->name + " народився спадкоємець: " + child.FullName(),
                          clan->color);
            }
        }
    }

    void DynastySystem::ResolveDeaths(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 baseChance = config.Float("characters/deathBaseChance", 0.004f);
        const f32 ageFactor = config.Float("characters/deathAgeFactor", 0.0009f);
        const i32 maxAge = config.Int("characters/maxAge", 92);

        Random& random = GlobalRandom();
        std::vector<EntityId> deceased;

        for (const auto& [id, character] : world.Characters())
        {
            if (!character.alive || !character.noble) continue;

            // Mortality rises steeply with age, with a hard ceiling on a long life.
            const f32 risk = baseChance + ageFactor * static_cast<f32>(character.age * character.age) / 40.0f;
            if (character.age >= maxAge || random.Chance(risk)) deceased.push_back(id);
        }

        std::vector<EntityId> clansNeedingHeir;
        for (EntityId id : deceased)
        {
            Character* character = world.FindCharacter(id);
            if (!character) continue;

            character->alive = false;
            if (Character* spouse = world.FindCharacter(character->spouse))
            {
                spouse->spouse = kInvalidId;
            }

            Clan* clan = world.FindClan(character->clan);
            if (!clan) continue;

            world.Log(character->FullName() + " з роду " + clan->name + " помер у " +
                      std::to_string(character->age) + " років", Color::FromRGB(0x8B97A4));

            if (clan->head == id) clansNeedingHeir.push_back(clan->id);
        }

        for (EntityId clanId : clansNeedingHeir)
        {
            if (Clan* clan = world.FindClan(clanId)) Inherit(world, *clan);
        }
    }

    void DynastySystem::Inherit(World& world, Clan& clan)
    {
        const EntityId heir = HeirOf(world, clan.id);
        if (heir == kInvalidId)
        {
            // The line is extinct; the realm's leading house takes the lands directly.
            State* state = world.FindState(clan.state);
            if (state && state->leader != clan.id)
            {
                Clan* liege = world.FindClan(state->leader);
                if (liege)
                {
                    const std::vector<EntityId> lands = clan.settlements;
                    for (EntityId settlementId : lands)
                    {
                        if (Settlement* settlement = world.FindSettlement(settlementId))
                        {
                            settlement->owner = liege->id;
                            liege->AddSettlement(settlementId);
                        }
                    }
                    clan.settlements.clear();
                }
            }
            clan.eliminated = true;
            world.Log("Рід " + clan.name + " урвався", Color::FromRGB(0xC05046));
            if (State* owner = world.FindState(clan.state)) owner->RemoveClan(clan.id);
            return;
        }

        clan.head = heir;
        if (const Character* lord = world.FindCharacter(heir))
        {
            world.Log(lord->FullName() + " успадковує рід " + clan.name, clan.color);
        }
    }
}
