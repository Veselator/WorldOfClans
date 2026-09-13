#include "PopulationSystem.h"
#include "CoverageSystem.h"
#include "../Factories/EvaluatorFactory.h"
#include "../Factories/SettlementFactory.h"
#include "../Factories/UnitFactory.h"
#include "../World/RaceDatabase.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Random.h"

#include <algorithm>

namespace woc
{
    f32& PopulationSystem::MigrantsFor(EntityId clanId)
    {
        for (auto& entry : m_migrantPool)
        {
            if (entry.first == clanId) return entry.second;
        }
        m_migrantPool.emplace_back(clanId, 0.0f);
        return m_migrantPool.back().second;
    }

    void PopulationSystem::Tick(World& world)
    {
        for (auto& [id, settlement] : world.Settlements())
        {
            UpdateProsperity(world, settlement);
            UpdateLoyalty(world, settlement);
            GrowPopulation(world, settlement);
        }
        HandleRevolts(world);
        SpawnVillages(world);
    }

    void PopulationSystem::GrowPopulation(World& world, Settlement& settlement)
    {
        ConfigManager& config = ConfigManager::Get();
        const RaceInfo& race = RaceDatabase::Get().Race(settlement.raceId);
        const SettlementTier& tier = settlement.Tier();

        f32 rate = config.Float("population/baseGrowthRate", 0.012f) * race.modifiers.populationGrowth;
        rate += settlement.ProsperityFraction() * config.Float("population/prosperityGrowthFactor", 0.02f);
        rate += (settlement.loyalty - 0.5f) * config.Float("population/loyaltyGrowthFactor", 0.008f);

        // Past the tier's ceiling growth stalls and the surplus becomes migrants.
        const f32 ceiling = static_cast<f32>(tier.maxPopulation);
        const f32 crowding = static_cast<f32>(settlement.population) / std::max(1.0f, ceiling);
        if (crowding > 0.9f)
        {
            rate *= 1.0f - std::min(1.0f, (crowding - 0.9f) * 10.0f) *
                           config.Float("population/overcrowdingPenalty", 0.5f);
        }

        if (settlement.besiegedBy != kInvalidId) rate = std::min(rate, -0.02f);

        const f32 growth = static_cast<f32>(settlement.population) * rate;
        settlement.population = std::max(20, settlement.population + static_cast<i32>(growth));

        // Overflow leaves in search of new land.
        if (settlement.population > tier.maxPopulation)
        {
            const i32 surplus = settlement.population - tier.maxPopulation;
            settlement.population -= surplus / 2;
            if (settlement.owner != kInvalidId)
            {
                MigrantsFor(settlement.owner) += static_cast<f32>(surplus / 2);
            }
        }
        else if (settlement.kind == SettlementKind::Village && settlement.owner != kInvalidId)
        {
            MigrantsFor(settlement.owner) += static_cast<f32>(settlement.population) *
                                             config.Float("population/migrantsPerMonthFactor", 0.004f);
        }
    }

    void PopulationSystem::UpdateProsperity(World& world, Settlement& settlement)
    {
        ConfigManager& config = ConfigManager::Get();
        const SettlementTier& tier = settlement.Tier();
        const f32 drift = config.Float("population/prosperityDrift", 0.01f);

        const f32 ceiling = std::max(1.0f, tier.prosperityCap);

        // Wealth converges on what this tier's order and farmland can sustain, in absolute coin.
        f32 target = ceiling * (0.4f + settlement.loyalty * 0.6f);

        // Fields around a settlement make it visibly richer.
        const f32 fields = world.Map().SampleField(settlement.position, 80.0f);
        target += fields * ceiling * 0.2f;
        if (settlement.besiegedBy != kInvalidId) target = ceiling * 0.05f;
        target = std::clamp(target, 0.0f, ceiling * 1.25f);

        settlement.prosperity += (target - settlement.prosperity) * drift * 3.0f;
        settlement.prosperity = std::max(0.0f, settlement.prosperity);
    }

    Json PopulationSystem::ToJson() const
    {
        Json list = Json::MakeArray();
        for (const auto& [clan, amount] : m_migrantPool)
        {
            Json pair = Json::MakeArray();
            pair.Push(static_cast<i64>(clan));
            pair.Push(amount);
            list.Push(pair);
        }
        return list;
    }

    void PopulationSystem::FromJson(const Json& node)
    {
        m_migrantPool.clear();
        for (const Json& pair : node.AsArray())
        {
            m_migrantPool.emplace_back(static_cast<EntityId>(pair[static_cast<size_t>(0)].AsNumber(0.0)),
                                       pair[static_cast<size_t>(1)].AsFloat(0.0f));
        }
    }

    f32 PopulationSystem::LoyaltyForecast(World& world, EntityId settlementId) const
    {
        const Settlement* settlement = world.FindSettlement(settlementId);
        if (!settlement) return 0.0f;

        ConfigManager& config = ConfigManager::Get();
        Scope<ISettlementEvaluator> evaluator = EvaluatorFactory::Build(*settlement, world.Map());

        f32 delta = evaluator->LoyaltyDrift();
        delta += RaceDatabase::Get().Faith(settlement->faithId).loyaltyBonus * 0.1f;

        const Clan* clan = world.FindClan(settlement->owner);
        if (clan)
        {
            const bool kin = settlement->raceId == clan->raceId;
            const bool faithful = settlement->faithId == clan->faithId;

            if (!kin)
                delta -= config.Float("population/loyaltyForeignRacePenalty", 0.18f) * 0.1f;
            if (!faithful)
                delta -= config.Float("population/loyaltyForeignFaithPenalty", 0.12f) * 0.1f;

            // A town of the lord's own people, praying to the lord's own gods, has no
            // quarrel with him. It used to be merely un-penalised, which left it drifting
            // downwards on distance alone until it rose against a prince of its own blood.
            if (kin && faithful)
            {
                delta += config.Float("population/loyaltyKinBonus", 0.05f);
            }

            // Distance from the clan's seat of power erodes obedience.
            const Settlement* capital = nullptr;
            for (EntityId owned : clan->settlements)
            {
                const Settlement* candidate = world.FindSettlement(owned);
                if (!candidate) continue;
                if (!capital || candidate->kind == SettlementKind::City) capital = candidate;
                if (capital && capital->kind == SettlementKind::City) break;
            }
            if (capital && capital->id != settlement->id)
            {
                const f32 distance = Distance(capital->position, settlement->position);
                f32 remoteness = distance * config.Float("population/loyaltyDistancePenalty", 0.00035f) *
                                 evaluator->DistancePenaltyMultiplier();
                // Distance tells far less on one's own countrymen: a far-off village of
                // your own people is still your people.
                if (kin && faithful) remoteness *= config.Float("population/loyaltyKinDistance", 0.35f);
                delta -= remoteness;
            }

            // And the one thing that does unsettle such a place: a new lord.
            if (world.Time().TotalDays() < settlement->newLordUntilDay)
            {
                delta -= config.Float("population/loyaltyNewLordPenalty", 0.05f);
            }
        }
        else
        {
            // Independent settlements drift towards contented self-rule.
            delta += 0.02f;
        }

        if (settlement->besiegedBy != kInvalidId) delta -= 0.06f;
        if (settlement->conversionDaysLeft > 0) delta -= 0.02f;
        return delta;
    }

    void PopulationSystem::UpdateLoyalty(World& world, Settlement& settlement)
    {
        settlement.loyalty = Clamp01(settlement.loyalty + LoyaltyForecast(world, settlement.id));
    }

    f32 PopulationSystem::RevoltChance(World& world, const Settlement& settlement) const
    {
        if (settlement.owner == kInvalidId) return 0.0f;
        if (settlement.besiegedBy != kInvalidId) return 0.0f;
        if (world.Time().TotalDays() < settlement.rebelliousUntilDay) return 0.0f;

        const Clan* clan = world.FindClan(settlement.owner);
        if (!clan) return 0.0f;

        ConfigManager& config = ConfigManager::Get();

        // A place of the lord's own people and the lord's own gods does not rise against
        // him at all - unless he has only just taken it, and even then the mood passes.
        if (settlement.raceId == clan->raceId && settlement.faithId == clan->faithId &&
            world.Time().TotalDays() >= settlement.newLordUntilDay)
        {
            return 0.0f;
        }

        // Discontent is the ground a revolt grows in; below the threshold there is none.
        const f32 threshold = config.Float("population/revoltThreshold", 0.35f);
        if (settlement.loyalty >= threshold) return 0.0f;

        f32 chance = config.Float("population/revoltBaseChance", 0.02f) *
                     ((threshold - settlement.loyalty) / std::max(0.01f, threshold));

        // A foreign people is the strongest reason to rise; a foreign god a lesser one.
        if (settlement.raceId != clan->raceId)
            chance += config.Float("population/revoltForeignRaceChance", 0.16f);
        if (settlement.faithId != clan->faithId)
            chance += config.Float("population/revoltForeignFaithChance", 0.06f);

        // Outside anyone's reach there is no garrison within a week's march, and everybody
        // in the village knows it.
        if (!CoverageSystem::Get().IsCovered(world, settlement.position))
            chance *= config.Float("population/revoltUncoveredFactor", 2.5f);

        // Troops quartered in the settlement keep the peace whatever else is true.
        for (const auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.garrisonOf == settlement.id && cohort.clan == settlement.owner)
            {
                chance *= config.Float("population/revoltGarrisonFactor", 0.2f);
                break;
            }
        }

        return Clamp01(chance);
    }

    EntityId PopulationSystem::RaiseRebels(World& world, Settlement& settlement)
    {
        ConfigManager& config = ConfigManager::Get();
        Random& random = GlobalRandom();
        const UnitDatabase& db = UnitDatabase::Get();

        // How many take up arms. A village turns out roughly one in sixteen - the men of
        // fighting age who own a spear or an axe - and no two risings are the same size.
        const f32 share = config.Float("population/rebelShareOfPopulation", 0.06f);
        const f32 spread = config.Float("population/rebelShareSpread", 0.4f);
        const f32 roll = share * random.RangeF(1.0f - spread, 1.0f + spread);

        i32 heads = static_cast<i32>(static_cast<f32>(settlement.population) * roll + 0.5f);
        heads = std::clamp(heads,
                           config.Int("population/rebelMinHeads", 24),
                           config.Int("population/rebelMaxHeads", 320));
        heads = std::min(heads, settlement.population / 3);
        if (heads <= 0) return kInvalidId;

        // Nobody's men: a rising answers to no clan, which is exactly what makes the place
        // unclaimed ground until somebody stands on it again.
        Cohort& rebels = UnitFactory::CreateCohort(world, kInvalidId, settlement.position, random,
                                                   "Повстанці — " + settlement.name);
        rebels.garrisonOf = settlement.id;
        rebels.currentTask.type = TaskType::Garrison;
        rebels.currentTask.targetSettlement = settlement.id;
        rebels.organisation = 0.6f;
        rebels.supply = 1.0f;

        // Spears at the front, whatever bows the village owns behind them. Villagers are
        // badly drilled and very angry, which is what the stat block below says.
        const u32 perUnit = std::max(20u, UnitFactory::EstablishmentFor(settlement));
        const f32 moraleBonus = config.Float("population/rebelMoraleBonus", 0.15f);

        i32 remaining = heads;
        u32 raised = 0;
        while (remaining > 0 && raised < db.MaxUnitsPerCohort())
        {
            const u32 batch = static_cast<u32>(std::min<i32>(remaining, static_cast<i32>(perUnit)));
            const UnitRole role = (raised % 3 == 2) ? UnitRole::Archer : UnitRole::Swordsman;

            Unit& unit = UnitFactory::Create(world, rebels.id, settlement.raceId, role,
                                             batch, settlement.name, random);
            unit.morale = Clamp01(unit.morale + moraleBonus);
            unit.training = Clamp01(unit.training * 0.45f);

            remaining -= static_cast<i32>(batch);
            ++raised;
        }

        if (rebels.units.empty())
        {
            world.DestroyCohort(rebels.id);
            return kInvalidId;
        }

        // They came out of the fields, so the fields are that much emptier.
        settlement.population = std::max(20, settlement.population - (heads - remaining));
        return rebels.id;
    }

    void PopulationSystem::BleedCohort(World& world, EntityId cohortId, f32 fraction, f32 killShare)
    {
        Cohort* cohort = world.FindCohort(cohortId);
        if (!cohort || fraction <= 0.0f) return;

        Random& random = GlobalRandom();
        std::vector<EntityId> emptied;

        for (EntityId unitId : cohort->units)
        {
            Unit* unit = world.FindUnit(unitId);
            if (!unit || unit->characters.empty()) continue;

            const f32 exact = Clamp01(fraction) * static_cast<f32>(unit->characters.size());
            u32 falling = static_cast<u32>(exact);
            if (random.Chance(exact - static_cast<f32>(falling))) ++falling;
            falling = std::min<u32>(falling, unit->Strength());

            for (u32 i = 0; i < falling; ++i)
            {
                const EntityId personId = unit->characters.back();
                unit->characters.pop_back();
                if (random.Chance(Clamp01(killShare))) world.Characters().erase(personId);
                else unit->wounded.push_back(personId);
            }
            unit->morale = Clamp01(unit->morale - fraction * 0.8f);
            if (unit->IsGone()) emptied.push_back(unitId);
        }

        for (EntityId unitId : emptied) world.DestroyUnit(unitId);
        if (cohort->IsEmpty()) world.DestroyCohort(cohortId);
    }

    void PopulationSystem::HandleRevolts(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        Random& random = GlobalRandom();

        std::vector<EntityId> revolting;
        for (const auto& [id, settlement] : world.Settlements())
        {
            const f32 chance = RevoltChance(world, settlement);
            if (chance <= 0.0f) continue;
            if (random.RangeF(0.0f, 1.0f) > chance) continue;
            revolting.push_back(id);
        }

        const State* human = world.HumanState();

        for (EntityId id : revolting)
        {
            Settlement* settlement = world.FindSettlement(id);
            if (!settlement) continue;
            Clan* clan = world.FindClan(settlement->owner);

            // Villages simply stop obeying; towns and castles need an army to be lost.
            if (settlement->kind != SettlementKind::Village) continue;

            const EntityId rebelsId = RaiseRebels(world, *settlement);
            if (rebelsId == kInvalidId) continue;   // nobody left in the village to rise

            // Is the lord's garrison in the place? Then the question is settled at the gate,
            // this same day, and the villagers usually lose - which is the whole reason a
            // lord quarters men in a restless village.
            std::vector<EntityId> defenders;
            for (const auto& [cohortId, cohort] : world.Cohorts())
            {
                if (cohort.garrisonOf != id || cohort.IsEmpty()) continue;
                if (cohort.clan != settlement->owner) continue;
                defenders.push_back(cohortId);
            }

            f32 loyalPower = 0.0f;
            for (EntityId cohortId : defenders) loyalPower += world.CohortPower(cohortId);
            const f32 rebelPower = world.CohortPower(rebelsId);

            const bool watched = human && clan && clan->state == human->id;

            if (loyalPower > 0.0f && loyalPower >= rebelPower)
            {
                // Put down. The garrison pays for it in proportion to what it faced.
                const f32 toll = std::min(0.5f, rebelPower / std::max(1.0f, loyalPower) * 0.35f);
                for (EntityId cohortId : defenders) BleedCohort(world, cohortId, toll, 0.45f);
                world.DestroyCohort(rebelsId);

                settlement->loyalty = Clamp01(settlement->loyalty + 0.18f);
                settlement->rebelliousUntilDay = world.Time().TotalDays() +
                    config.Int("population/revoltCooldownDays", 720) / 2;

                world.Log(settlement->name + ": заколот придушено гарнізоном",
                          clan ? clan->color : Color::FromRGB(0xD2933A));
                world.MarkRevolt(settlement->position);
                if (watched)
                {
                    world.Announce("ЗАКОЛОТ ПРИДУШЕНО", settlement->name + " — гарнізон устояв",
                                   Color::FromRGB(0xD2933A));
                }
                continue;
            }

            // The garrison is too thin, or there is none: the men at the gate are overrun
            // and the village is out of the lord's hands.
            for (EntityId cohortId : defenders) BleedCohort(world, cohortId, 0.65f, 0.5f);

            if (clan) clan->RemoveSettlement(id);
            settlement->owner = kInvalidId;
            settlement->heldByPresence = false;
            settlement->loyalty = 0.6f;
            settlement->rebelliousUntilDay = world.Time().TotalDays() +
                config.Int("population/revoltCooldownDays", 720);

            const Clan* former = clan;
            const std::string why = former && settlement->raceId != former->raceId
                ? " піднявся: чужий рід не вкаже тутешньому народові"
                : (former && settlement->faithId != former->faithId
                    ? " піднявся: чужі боги не миліші за своїх"
                    : " відклався й більше нікому не платить данини");
            world.Log(settlement->name + why, Color::FromRGB(0xD2933A));
            world.MarkRevolt(settlement->position);
            if (watched)
            {
                world.Announce("ПОВСТАННЯ",
                               settlement->name + " піднявся проти роду " +
                               (former ? former->name : std::string("вашого")),
                               Color::FromRGB(0xD2933A));
            }
            CoverageSystem::Get().MarkDirty();
        }
    }

    void PopulationSystem::SpawnVillages(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 required = config.Float("population/migrantsForNewVillage", 260.0f);
        const f32 searchRadius = config.Float("population/newVillageMaxDistanceFromParent", 220.0f);

        Random& random = GlobalRandom();

        for (auto& [clanId, migrants] : m_migrantPool)
        {
            if (migrants < required) continue;

            Clan* clan = world.FindClan(clanId);
            if (!clan || clan->settlements.empty()) { migrants = 0.0f; continue; }

            // New villages sprout near an existing one - people move, they do not teleport.
            const EntityId parentId = clan->settlements[
                static_cast<size_t>(random.Range(0, static_cast<i32>(clan->settlements.size()) - 1))];
            const Settlement* parent = world.FindSettlement(parentId);
            if (!parent) { migrants = 0.0f; continue; }

            Vec2 site;
            // Migrants settle inside their own realm too: a band of peasants does not found
            // a village in a rival's country.
            if (!SettlementFactory::FindSite(world, world.Map(), SettlementKind::Village,
                                             parent->position, searchRadius, random, site, clanId))
            {
                // Nowhere to put them this month; keep the pool and try again later.
                continue;
            }

            SettlementRequest request;
            request.kind = SettlementKind::Village;
            request.position = site;
            request.raceId = parent->raceId;
            request.faithId = parent->faithId;
            request.owner = clanId;
            request.population = static_cast<i32>(required * 0.7f);

            Settlement& village = SettlementFactory::Create(world, request, random);
            migrants -= required;

            world.Log("Переселенці заснували село " + village.name, clan->color);
            CoverageSystem::Get().MarkDirty();
        }
    }
}
