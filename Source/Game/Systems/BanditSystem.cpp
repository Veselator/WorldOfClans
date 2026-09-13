#include "BanditSystem.h"

#include "CoverageSystem.h"
#include "MovementSystem.h"
#include "../Factories/NamePool.h"
#include "../Factories/CharacterFactory.h"
#include "../Factories/UnitFactory.h"
#include "../Map/TerrainTypes.h"
#include "../World/RaceDatabase.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"

#include <algorithm>
#include <cmath>

namespace woc
{
    namespace
    {
        /// Every band flies the same colour, whatever people it is drawn from and whoever
        /// it answers to: on a map of realms, the thing a player needs to recognise at a
        /// glance is that these are nobody's men.
        constexpr u32 kOutlawColor = 0x474747;
    }

    Json BanditSettings::ToJson() const
    {
        Json node = Json::MakeObject();
        node["enabled"] = enabled;
        node["density"] = density;
        return node;
    }

    BanditSettings BanditSettings::FromJson(const Json& node)
    {
        BanditSettings settings;
        settings.enabled = node["enabled"].AsBool(false);
        settings.density = node["density"].AsFloat(0.5f);
        return settings;
    }

    bool BanditSystem::IsOutlaw(const World& world, EntityId clanId)
    {
        const State* state = world.StateOfClan(clanId);
        return state && state->outlaw;
    }

    ResourceData BanditSystem::Spoils(const BanditCamp& camp)
    {
        ConfigManager& config = ConfigManager::Get();

        // What they buried, plus what the camp itself is worth once it is pulled apart:
        // tents, carts, a winter's stolen grain.
        ResourceData spoils = camp.hoard;
        spoils.wood += camp.strength * config.Float("bandits/campSalvageWood", 40.0f);
        spoils.food += camp.strength * config.Float("bandits/campSalvageFood", 30.0f);
        return spoils;
    }

    void BanditSystem::Reset()
    {
        m_settings = BanditSettings{};
        m_carry = 0.0f;
    }

    // =========================================================================================
    // Putting them on the map
    // =========================================================================================

    bool BanditSystem::FindCampSite(World& world, Random& random, Vec2& out) const
    {
        const MapData& map = world.Map();
        ConfigManager& config = ConfigManager::Get();

        const f32 fromSeats = config.Float("bandits/minDistanceFromSettlement", 190.0f);
        const f32 fromCamps = config.Float("bandits/minDistanceBetweenCamps", 320.0f);

        // Two passes. The first looks for the country robbers would actually choose - deep
        // woods, broken hills, well away from anybody. The second will take any quiet spot
        // at all, because a map of open plain would otherwise have no robbers on it, and
        // "there happen to be no woods here" is a poor reason for the option to do nothing.
        const f32 wantedCover = config.Float("bandits/minCover", 0.06f);
        const i32 attempts = config.Int("bandits/placementAttempts", 400);

        for (i32 attempt = 0; attempt < attempts; ++attempt)
        {
            const bool picky = attempt < attempts / 2;

            const Vec2 probe{
                random.RangeF(40.0f, static_cast<f32>(map.PixelWidth()) - 40.0f),
                random.RangeF(40.0f, static_cast<f32>(map.PixelHeight()) - 40.0f)
            };

            const Coord tile = map.ToTile(probe);
            if (!map.InBounds(tile) || !map.IsPassable(tile)) continue;

            const TerrainInfo& info = map.TerrainAt(tile);
            if (info.water) continue;

            if (picky)
            {
                const f32 cover = map.SampleForest(probe, 80.0f) + (info.mineable ? 0.5f : 0.0f);
                if (cover < wantedCover) continue;
            }

            // ...and the distances relax with the passes too, so a crowded map still gets
            // its camps rather than silently getting none.
            const f32 seatRoom = picky ? fromSeats : fromSeats * 0.6f;
            const f32 campRoom = picky ? fromCamps : fromCamps * 0.6f;

            bool clear = true;
            for (const auto& [id, settlement] : world.Settlements())
            {
                if (Distance(settlement.position, probe) < seatRoom) { clear = false; break; }
            }
            if (!clear) continue;

            for (const BanditCamp& camp : world.BanditCamps())
            {
                if (Distance(camp.position, probe) < campRoom) { clear = false; break; }
            }
            if (!clear) continue;

            out = probe;
            return true;
        }
        return false;
    }

    void BanditSystem::Seed(World& world, const BanditSettings& settings, Random& random)
    {
        m_settings = settings;
        m_carry = 0.0f;
        m_random.Seed(world.Seed() ^ 0x8ADC17u);

        if (!settings.enabled) return;

        ConfigManager& config = ConfigManager::Get();
        const RaceDatabase& races = RaceDatabase::Get();

        // How many camps the country carries: a share of its area, scaled by how thick the
        // player asked for it. A dense setting on a big map is a serious nuisance; a thin
        // one on a small map is a rumour.
        const f32 area = static_cast<f32>(world.Map().PixelWidth()) *
                         static_cast<f32>(world.Map().PixelHeight());
        const f32 perArea = config.Float("bandits/campsPerMillionPixels", 2.6f);
        const f32 wanted = area / 1000000.0f * perArea *
                           (0.35f + Clamp01(settings.density) * 1.65f);

        const i32 count = std::clamp(static_cast<i32>(wanted + 0.5f), 1,
                                     config.Int("bandits/maxCamps", 24));

        // Each camp is its own band with its own name, so the chronicle can tell them apart
        // and so two of them meeting in the same wood have something to fight about.
        const i32 clanCount = std::max(1, std::min(count, config.Int("bandits/bands", 4)));

        std::vector<EntityId> outlawClans;
        for (i32 i = 0; i < clanCount; ++i)
        {
            const std::vector<RaceInfo>& raceList = races.Races();
            const std::string raceId = raceList.empty()
                ? std::string("human")
                : raceList[static_cast<size_t>(random.Range(0, static_cast<i32>(raceList.size()) - 1))].id;

            State& state = world.CreateState();
            state.outlaw = true;
            state.raceId = raceId;
            state.color = Color::FromRGB(kOutlawColor);
            state.name = "Ватага " + NamePool::Get().ClanName(raceId, random);

            Clan& clan = world.CreateClan();
            clan.state = state.id;
            clan.raceId = raceId;
            clan.faithId = races.Race(raceId).defaultFaith;
            clan.name = state.name;
            clan.color = Color::FromRGB(kOutlawColor);
            clan.resources = {};
            state.AddClan(clan.id);

            outlawClans.push_back(clan.id);
        }

        i32 placed = 0;
        for (i32 i = 0; i < count; ++i)
        {
            Vec2 site;
            if (!FindCampSite(world, random, site)) continue;

            const EntityId clanId = outlawClans[static_cast<size_t>(i) % outlawClans.size()];
            const Clan* clan = world.FindClan(clanId);

            BanditCamp& camp = world.CreateBanditCamp();
            camp.clan = clanId;
            camp.position = site;
            camp.raceId = clan ? clan->raceId : std::string("human");
            camp.strength = config.Float("bandits/campStrength", 60.0f) *
                            random.RangeF(0.75f, 1.35f);
            camp.damage = 0.0f;
            camp.musterDays = random.RangeF(0.0f, config.Float("bandits/musterDays", 90.0f));
            ++placed;
        }

        WOC_LOG_INFO("Bandits: ", placed, " camps in ", outlawClans.size(), " bands");
    }

    // =========================================================================================
    // The daily business of robbery
    // =========================================================================================

    void BanditSystem::Tick(World& world, f32 days)
    {
        if (!m_settings.enabled || days <= 0.0f) return;

        // The assault on a camp is the one thing that has to follow the clock closely,
        // because the player is watching it happen.
        ResolveAssaults(world, days);

        m_carry += days;
        if (m_carry < 1.0f) return;

        const f32 elapsed = std::floor(m_carry);
        m_carry -= elapsed;

        MusterBands(world, elapsed);
        Replenish(world, elapsed);
        DirectBands(world);
    }

    i32 BanditSystem::BandsOf(const World& world, EntityId campId) const
    {
        i32 count = 0;
        for (const auto& [id, cohort] : world.Cohorts())
        {
            if (cohort.homeCamp == campId && !cohort.IsEmpty()) ++count;
        }
        return count;
    }

    EntityId BanditSystem::RaiseBand(World& world, BanditCamp& camp, Random& random)
    {
        const UnitDatabase& db = UnitDatabase::Get();
        ConfigManager& config = ConfigManager::Get();

        Cohort& band = UnitFactory::CreateCohort(world, camp.clan, camp.position, random,
                                                 "Ватага з лісу");
        band.homeCamp = camp.id;
        band.mayRaid = true;           // it is the only thing they do
        band.organisation = 0.85f;
        band.supply = 1.0f;
        band.currentTask.Clear();

        // A band is a handful of men with what they could steal: mostly spears, a bow or
        // two, and a horse where the camp has been lucky.
        const i32 units = random.Range(config.Int("bandits/minUnitsPerBand", 2),
                                       config.Int("bandits/maxUnitsPerBand", 4));
        const u32 headCount = static_cast<u32>(config.Int("bandits/headsPerUnit", 28));

        for (i32 i = 0; i < units; ++i)
        {
            UnitRole role = UnitRole::Swordsman;
            if (i % 3 == 1) role = UnitRole::Archer;
            else if (i % 5 == 4) role = UnitRole::HorseArcher;

            Unit& unit = UnitFactory::Create(world, band.id, camp.raceId, role,
                                             static_cast<u32>(headCount * random.RangeF(0.7f, 1.15f)),
                                             "Ліс", random);
            // They have never drilled a day in their lives, and they know it.
            unit.training = Clamp01(db.BaseTraining() * 0.4f);
            unit.morale = Clamp01(unit.morale * 0.9f);
        }
        return band.id;
    }

    void BanditSystem::MusterBands(World& world, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();
        const i32 maxBands = config.Int("bandits/maxBandsPerCamp", 3);
        const f32 musterDays = std::max(1.0f, config.Float("bandits/musterDays", 90.0f) /
                                              (0.5f + Clamp01(m_settings.density)));

        for (BanditCamp& camp : world.BanditCamps())
        {
            camp.musterDays -= days;
            if (camp.musterDays > 0.0f) continue;

            camp.musterDays = musterDays * m_random.RangeF(0.7f, 1.4f);
            if (BandsOf(world, camp.id) >= maxBands) continue;

            RaiseBand(world, camp, m_random);
        }
    }

    void BanditSystem::Replenish(World& world, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 rate = config.Float("bandits/replenishPerDay", 0.02f);

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.homeCamp == kInvalidId) continue;
            const BanditCamp* camp = world.FindBanditCamp(cohort.homeCamp);
            if (!camp) continue;
            if (Distance(cohort.position, camp->position) > 60.0f) continue;

            // Home, and taking on whoever the roads have turned out this season.
            cohort.supply = Clamp01(cohort.supply + 0.1f * days);
            cohort.organisation = Clamp01(cohort.organisation + 0.05f * days);

            for (EntityId unitId : cohort.units)
            {
                Unit* unit = world.FindUnit(unitId);
                if (!unit || unit->Strength() >= unit->establishment) continue;

                const f32 expected = rate * days * static_cast<f32>(unit->establishment);
                i32 joining = static_cast<i32>(expected);
                if (m_random.Chance(expected - static_cast<f32>(joining))) ++joining;

                for (i32 i = 0; i < joining && unit->Strength() < unit->establishment; ++i)
                {
                    Character& person = CharacterFactory::CreateCommoner(world, unit->raceId,
                                                                         "Ліс", m_random);
                    unit->characters.push_back(person.id);
                }
            }
        }
    }

    void BanditSystem::DirectBands(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        MovementSystem& movement = MovementSystem::Get();

        const f32 range = config.Float("bandits/raidRange", 700.0f);
        const f32 goHome = config.Float("bandits/retreatStrength", 0.45f);

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.homeCamp == kInvalidId || cohort.IsEmpty()) continue;
            if (cohort.inBattle || cohort.IsWithdrawing()) continue;
            if (cohort.currentTask.IsMoving()) continue;

            BanditCamp* camp = world.FindBanditCamp(cohort.homeCamp);

            // What they took goes back to the camp and under the floor of it. That is what
            // makes burning a camp worth the march.
            if (camp && Distance(cohort.position, camp->position) < 60.0f)
            {
                const Clan* clan = world.FindClan(cohort.clan);
                if (clan && (clan->resources.money > 0.5f || clan->resources.food > 0.5f))
                {
                    Clan* purse = world.FindClan(cohort.clan);
                    camp->hoard += purse->resources;
                    purse->resources = {};
                }
            }

            // A mauled band goes home rather than dying in a field. Without a camp to go to
            // it simply keeps robbing until somebody stops it.
            f32 whole = 0.0f;
            f32 muster = 0.0f;
            for (EntityId unitId : cohort.units)
            {
                const Unit* unit = world.FindUnit(unitId);
                if (!unit) continue;
                whole += static_cast<f32>(unit->establishment);
                muster += static_cast<f32>(unit->Strength());
            }
            const f32 condition = whole > 0.0f ? muster / whole : 0.0f;

            if (camp && (condition < goHome || cohort.supply < 0.3f))
            {
                if (Distance(cohort.position, camp->position) > 60.0f)
                {
                    movement.OrderTask(world, cohortId, TaskType::Move, camp->position);
                }
                continue;
            }

            // Otherwise: the nearest village worth the ride. Whose it is does not matter -
            // that is rather the point of them.
            const Settlement* target = nullptr;
            f32 best = range;
            for (const auto& [id, settlement] : world.Settlements())
            {
                if (settlement.UnderConstruction()) continue;
                const f32 distance = Distance(cohort.position, settlement.position);
                if (distance >= best) continue;

                // A place with a garrison in it is a fight, not a robbery; they prefer the
                // ones with nobody at home, and only try a held one when it is very close.
                bool held = false;
                for (const auto& [otherId, other] : world.Cohorts())
                {
                    if (other.garrisonOf == id && !other.IsEmpty()) { held = true; break; }
                }
                if (held && distance > range * 0.25f) continue;

                best = distance;
                target = &settlement;
            }

            if (!target) continue;
            movement.OrderTask(world, cohortId, TaskType::Raid, target->position, target->id);
        }
    }

    // =========================================================================================
    // Burning one out
    // =========================================================================================

    void BanditSystem::ResolveAssaults(World& world, f32 days)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 pace = config.Float("bandits/stormPace", 0.35f);

        std::vector<std::pair<EntityId, EntityId>> burnt;   // camp, whoever burnt it

        for (auto& [cohortId, cohort] : world.Cohorts())
        {
            if (cohort.currentTask.type != TaskType::Storm) continue;
            if (cohort.currentTask.IsMoving()) continue;

            BanditCamp* camp = world.FindBanditCamp(cohort.currentTask.targetSettlement);
            if (!camp) { cohort.currentTask.Clear(); continue; }
            if (Distance(cohort.position, camp->position) > 50.0f) continue;

            // A camp is tents and a ditch, not a wall: what decides it is how many men are
            // pulling the tents down and whether the band is at home to object.
            f32 defence = camp->strength;
            for (const auto& [otherId, other] : world.Cohorts())
            {
                if (other.clan != camp->clan || other.IsEmpty()) continue;
                if (Distance(other.position, camp->position) > 60.0f) continue;
                defence += world.CohortPower(otherId) * 0.02f;
            }

            const f32 attack = world.CohortPower(cohortId) * 0.02f;
            camp->damage += attack / std::max(1.0f, defence) * pace * days * camp->strength;

            if (camp->damage >= camp->strength) burnt.emplace_back(camp->id, cohort.clan);
        }

        for (const auto& [campId, clanId] : burnt)
        {
            const BanditCamp* camp = world.FindBanditCamp(campId);
            if (!camp) continue;

            const ResourceData spoils = Spoils(*camp);
            if (Clan* clan = world.FindClan(clanId))
            {
                clan->resources += spoils;
                world.Log("Розбійницький табір спалено: " +
                          std::to_string(static_cast<i32>(spoils.money)) + " срібла, " +
                          std::to_string(static_cast<i32>(spoils.food)) + " їжі, " +
                          std::to_string(static_cast<i32>(spoils.wood)) + " дерева",
                          clan->color);
            }

            // The bands that called it home are now homeless; they keep robbing until
            // somebody rides them down.
            for (auto& [cohortId, cohort] : world.Cohorts())
            {
                if (cohort.homeCamp == campId) cohort.homeCamp = kInvalidId;
                if (cohort.currentTask.type == TaskType::Storm &&
                    cohort.currentTask.targetSettlement == campId)
                {
                    cohort.currentTask.Clear();
                }
            }
            world.DestroyBanditCamp(campId);
        }
    }

    // =========================================================================================
    // Persistence
    // =========================================================================================

    Json BanditSystem::ToJson() const
    {
        Json node = Json::MakeObject();
        node["settings"] = m_settings.ToJson();
        node["carry"] = m_carry;
        node["random"] = m_random.State();
        return node;
    }

    void BanditSystem::FromJson(const Json& node)
    {
        m_settings = BanditSettings::FromJson(node["settings"]);
        m_carry = node["carry"].AsFloat(0.0f);
        m_random.SetState(node["random"].AsString());
    }
}
