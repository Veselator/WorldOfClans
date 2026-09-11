#include "AIPlayer.h"
#include "../Factories/SettlementFactory.h"
#include "../Factories/UnitFactory.h"
#include "../Systems/BattleSystem.h"
#include "../Systems/DiplomacySystem.h"
#include "../Systems/EconomySystem.h"
#include "../Systems/MovementSystem.h"
#include "../Systems/RoadSystem.h"
#include "../Systems/SettlementSystem.h"
#include "../World/World.h"
#include "../../Core/Config.h"

#include <algorithm>
#include <cmath>

namespace woc
{
    AIPlayer::AIPlayer(EntityId stateId, u32 seed)
        : IPlayer(stateId), m_random(seed)
    {
        m_profile = AIProfile::Pick(m_random);
    }

    void AIPlayer::OnThink(World& world, i32 day)
    {
        State* state = world.FindState(m_stateId);
        if (!state || state->eliminated) return;

        // Every clan of the realm acts for itself; the leading house acts first.
        std::vector<EntityId> order = state->clans;
        std::sort(order.begin(), order.end(),
            [state](EntityId a, EntityId b) { return a == state->leader && b != state->leader; });

        for (EntityId clanId : order)
        {
            Clan* clan = world.FindClan(clanId);
            if (!clan || clan->eliminated) continue;

            const RealmShape realm = MeasureRealm(world, *clan);

            ManageEconomy(world, *clan);
            ManageMilitary(world, *clan, realm);
            if (clanId == state->leader) ManageDiplomacy(world, *clan, realm);

            // Expansion is deliberately slow: founding towns everywhere at once looks silly.
            if (day - m_lastExpansionDay > m_profile.expansionIntervalDays)
            {
                ManageExpansion(world, *clan);
                m_lastExpansionDay = day;
            }
        }
    }

    // =========================================================================================
    // Knowing where one's own land ends
    // =========================================================================================

    AIPlayer::RealmShape AIPlayer::MeasureRealm(const World& world, const Clan& clan)
    {
        RealmShape realm;
        if (clan.settlements.empty()) return realm;

        Vec2 sum;
        i32 counted = 0;
        for (EntityId settlementId : clan.settlements)
        {
            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;
            sum = sum + settlement->position;
            ++counted;
        }
        if (counted == 0) return realm;

        realm.center = sum * (1.0f / static_cast<f32>(counted));
        for (EntityId settlementId : clan.settlements)
        {
            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;
            realm.radius = std::max(realm.radius, Distance(settlement->position, realm.center));
        }

        // A single holding still owns the ground around it.
        realm.radius = std::max(realm.radius, 180.0f);
        realm.valid = true;
        return realm;
    }

    bool AIPlayer::WithinReach(const RealmShape& realm, const Vec2& position) const
    {
        if (!realm.valid) return true;   // landless: nothing left to defend, so range is moot
        return Distance(position, realm.center) <= realm.radius + m_profile.reachBeyondBorder;
    }

    // =========================================================================================
    // Economy
    // =========================================================================================

    f32 AIPlayer::ScarcityWeight(World& world, const Clan& clan, ResourceType resource) const
    {
        // What the realm already has of this, per month and in the treasury. A resource it
        // earns nothing of is worth several times one it is swimming in.
        const ClanBudget budget = EconomySystem::Get().Preview(world, clan.id);

        f32 income = 0.0f;
        f32 stock = 0.0f;
        switch (resource)
        {
        case ResourceType::Money: income = budget.income.money; stock = clan.resources.money; break;
        case ResourceType::Wood:  income = budget.income.wood;  stock = clan.resources.wood;  break;
        case ResourceType::Stone: income = budget.income.stone; stock = clan.resources.stone; break;
        case ResourceType::Food:  income = budget.income.food;  stock = clan.resources.food;  break;
        default: return 1.0f;
        }

        ConfigManager& config = ConfigManager::Get();
        const f32 want = config.Float("ai/resourceComfort", 18.0f);
        const f32 floorWeight = config.Float("ai/scarcityFloor", 0.6f);
        const f32 ceilWeight = config.Float("ai/scarcityCeiling", 3.2f);

        // Income counts for far more than a pile in the treasury: a pile runs out.
        const f32 supply = std::max(0.0f, income) + std::max(0.0f, stock) * 0.02f;
        const f32 weight = want / (want + supply * 2.0f) * ceilWeight;
        return std::clamp(weight, floorWeight, ceilWeight);
    }

    void AIPlayer::ManageEconomy(World& world, Clan& clan)
    {
        ConfigManager& config = ConfigManager::Get();
        SettlementSystem& settlements = SettlementSystem::Get();
        const f32 reserve = config.Float("ai/recruitTreasuryReserve", 120.0f);

        for (EntityId settlementId : clan.settlements)
        {
            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;
            if (!settlement->construction.empty()) continue;
            if (clan.resources.money < reserve) break;

            // Score the buildable options and take the best one that is actually affordable.
            const std::vector<BuildOption> options = settlements.BuildOptions(world, settlementId);
            const BuildingInfo* best = nullptr;
            f32 bestScore = 0.0f;

            for (const BuildOption& option : options)
            {
                if (!option.allowed || !option.affordable || !option.building) continue;

                f32 score = 0.0f;
                switch (option.building->decorator)
                {
                case DecoratorKind::Production:
                {
                    score = (option.building->multiplier - 1.0f) * 4.0f + option.building->flat * 0.5f;
                    score *= m_profile.economyWeight;
                    // Three units of stone and three of silver are not the same prize. A
                    // realm with no quarry anywhere has no walls and no castles either, and
                    // the old scoring - which compared raw output figures across resources -
                    // is why a quarry never once beat a sawmill to the top of the list.
                    score *= ScarcityWeight(world, clan, option.building->resource);
                    break;
                }
                case DecoratorKind::Defense:
                    score = (option.building->defenseMultiplier - 1.0f) * 3.0f * m_profile.defenceWeight;
                    break;
                case DecoratorKind::Loyalty:
                    // A restless town is worth calming before it is worth enriching.
                    score = option.building->loyaltyPerMonth * 60.0f * (1.2f - settlement->loyalty);
                    break;
                case DecoratorKind::Coverage:
                    score = (option.building->coverageMultiplier - 1.0f) * 6.0f * m_profile.expansionWeight;
                    break;
                case DecoratorKind::Military:
                    score = option.building->trainingBonus * 8.0f;
                    break;
                }

                // Cheaper is better when two options are otherwise comparable.
                score /= 1.0f + option.building->cost.money / 400.0f;
                if (score > bestScore) { bestScore = score; best = option.building; }
            }

            if (best) settlements.StartConstruction(world, settlementId, best->id);
        }

        ConnectHoldings(world, clan);
    }

    void AIPlayer::ConnectHoldings(World& world, Clan& clan)
    {
        // A road is worth building once the treasury can spare it: it speeds the host to
        // the frontier and, where it crosses a river, carries authority over with it.
        RoadSystem& roads = RoadSystem::Get();
        if (clan.settlements.size() < 2) return;
        if (!roads.Projects().empty()) return;   // one road at a time is enough

        const f32 reserve = ConfigManager::Get().Float("ai/recruitTreasuryReserve", 120.0f);
        if (clan.resources.money < reserve * 3.0f) return;

        RoadPlan best;
        f32 bestLength = 1e9f;

        for (size_t i = 0; i < clan.settlements.size(); ++i)
        {
            for (size_t j = i + 1; j < clan.settlements.size(); ++j)
            {
                const EntityId a = clan.settlements[i];
                const EntityId b = clan.settlements[j];
                if (roads.AreConnected(world, a, b)) continue;

                const Settlement* first = world.FindSettlement(a);
                const Settlement* second = world.FindSettlement(b);
                if (!first || !second) continue;
                // Only ever join neighbours: a road across the whole realm is nobody's plan.
                if (Distance(first->position, second->position) > 420.0f) continue;

                const RoadPlan plan = roads.Plan(world, a, b);
                if (!plan.valid || !clan.resources.CanAfford(plan.cost)) continue;
                if (plan.metres < bestLength) { bestLength = plan.metres; best = plan; }
            }
        }

        if (best.valid) roads.Begin(world, clan.id, best);
    }

    // =========================================================================================
    // Choosing where to send armies
    // =========================================================================================

    EntityId AIPlayer::PickThreatenedSettlement(World& world, const Clan& clan) const
    {
        EntityId worst = kInvalidId;
        f32 worstScore = 0.0f;

        for (EntityId settlementId : clan.settlements)
        {
            const Settlement* settlement = world.FindSettlement(settlementId);
            if (!settlement) continue;

            f32 score = 0.0f;
            if (settlement->besiegedBy != kInvalidId) score += 10.0f;
            score += (1.0f - settlement->loyalty) * 2.0f;

            // Enemy armies nearby matter more than distant ones.
            for (const auto& [cohortId, cohort] : world.Cohorts())
            {
                if (!world.AreHostile(clan.id, cohort.clan)) continue;
                const f32 distance = Distance(cohort.position, settlement->position);
                if (distance < 320.0f) score += (320.0f - distance) / 320.0f * 4.0f;
            }

            bool garrisoned = false;
            for (const auto& [cohortId, cohort] : world.Cohorts())
            {
                if (cohort.garrisonOf == settlementId) { garrisoned = true; break; }
            }
            if (!garrisoned) score *= 1.6f;

            // A wary lord reacts to a smaller threat than a bold one.
            if (score > worstScore) { worstScore = score; worst = settlementId; }
        }

        const f32 threshold = 2.0f * (m_profile.stance == AIStance::Aggressive ? 1.8f : 1.0f) *
                              (m_profile.stance == AIStance::Defensive ? 0.6f : 1.0f);
        return worstScore > threshold ? worst : kInvalidId;
    }

    EntityId AIPlayer::PickRallyPoint(World& world, const Clan& clan) const
    {
        // The frontier is wherever one's own land comes closest to someone else's.
        EntityId frontier = kInvalidId;
        f32 closest = 1e9f;

        for (EntityId settlementId : clan.settlements)
        {
            const Settlement* mine = world.FindSettlement(settlementId);
            if (!mine || mine->kind == SettlementKind::Village) continue;

            for (const auto& [otherId, other] : world.Settlements())
            {
                if (other.owner == clan.id || other.owner == kInvalidId) continue;
                const f32 distance = Distance(mine->position, other.position);
                if (distance < closest) { closest = distance; frontier = settlementId; }
            }
        }

        // No neighbours yet, or nothing but villages: hold the largest seat.
        if (frontier == kInvalidId)
        {
            i32 biggest = -1;
            for (EntityId settlementId : clan.settlements)
            {
                const Settlement* mine = world.FindSettlement(settlementId);
                if (!mine) continue;
                if (mine->population > biggest) { biggest = mine->population; frontier = settlementId; }
            }
        }
        return frontier;
    }

    void AIPlayer::Muster(World& world, Cohort& cohort, EntityId rally) const
    {
        const Settlement* post = world.FindSettlement(rally);
        if (!post) return;

        // Standing camp outside the walls, not inside them: the host must be seen on the
        // frontier, and a garrisoned army is invisible and slow to react.
        const f32 angle = static_cast<f32>(cohort.id % 8u) * 0.785f;
        const f32 spread = 55.0f + static_cast<f32>(cohort.id % 3u) * 18.0f;
        const Vec2 camp{ post->position.x + std::cos(angle) * spread,
                         post->position.y + std::sin(angle) * spread };

        if (Distance(cohort.position, camp) < 40.0f) return;   // already where it belongs
        MovementSystem::Get().OrderTask(world, cohort.id, TaskType::Move, camp);
    }

    EntityId AIPlayer::PickOffensiveTarget(World& world, const Clan& clan, const RealmShape& realm,
                                           const Vec2& from) const
    {
        const f32 limit = ConfigManager::Get().Float("ai/raidDistanceLimit", 900.0f);

        EntityId best = kInvalidId;
        f32 bestScore = 0.0f;

        for (const auto& [settlementId, settlement] : world.Settlements())
        {
            // War is the only licence to attack; unclaimed holdings belong to nobody.
            if (!world.MayAttackSettlement(clan.id, settlementId)) continue;

            // The decisive constraint: a campaign stays near one's own borders.
            if (!WithinReach(realm, settlement.position)) continue;

            const f32 distance = Distance(settlement.position, from);
            if (distance > limit) continue;

            // Value the prize, discount by how far away and how well defended it is.
            f32 score = static_cast<f32>(settlement.population) / 400.0f;
            if (settlement.kind == SettlementKind::City) score *= 1.8f;
            if (settlement.kind == SettlementKind::Castle) score *= 1.3f;
            if (settlement.owner == kInvalidId) score *= 0.8f;
            score *= 1.0f - distance / (limit * 1.4f);
            score /= 1.0f + BattleSystem::Get().SettlementDefense(world, settlementId) * 0.05f;

            // Ground just outside the frontier is worth more than ground deep in enemy land.
            if (realm.valid)
            {
                const f32 beyond = std::max(0.0f, Distance(settlement.position, realm.center) - realm.radius);
                score *= 1.0f / (1.0f + beyond / 300.0f);
            }

            if (score > bestScore) { bestScore = score; best = settlementId; }
        }
        return best;
    }

    // =========================================================================================
    // Military
    // =========================================================================================

    void AIPlayer::ManageMilitary(World& world, Clan& clan, const RealmShape& realm)
    {
        ConfigManager& config = ConfigManager::Get();
        SettlementSystem& settlements = SettlementSystem::Get();
        MovementSystem& movement = MovementSystem::Get();

        const i32 minGarrison = m_profile.minGarrisonUnits;
        const f32 reserve = config.Float("ai/recruitTreasuryReserve", 120.0f);

        // --- raise troops ---------------------------------------------------------------------
        if (clan.resources.money > reserve * 2.0f)
        {
            for (EntityId settlementId : clan.settlements)
            {
                const Settlement* settlement = world.FindSettlement(settlementId);
                if (!settlement) continue;
                if (settlement->kind == SettlementKind::Village) continue;

                EntityId garrison = kInvalidId;
                i32 units = 0;
                for (const auto& [cohortId, cohort] : world.Cohorts())
                {
                    if (cohort.garrisonOf != settlementId) continue;
                    garrison = cohortId;
                    units = static_cast<i32>(cohort.units.size());
                    break;
                }
                if (units >= minGarrison + 3) continue;

                // A balanced levy: line infantry first, then missiles, then horse.
                static const UnitRole kPreference[] = {
                    UnitRole::Swordsman, UnitRole::Archer, UnitRole::Swordsman, UnitRole::Cavalry
                };
                const UnitRole role = kPreference[static_cast<size_t>(m_random.Range(0, 3))];
                if (garrison == kInvalidId)
                {
                    settlements.Recruit(world, settlementId, kInvalidId, UnitRole::Aristocrat);
                }
                else
                {
                    settlements.Recruit(world, settlementId, garrison, role);
                }
                if (clan.resources.money < reserve * 2.0f) break;
            }
        }

        // --- give the armies something to do ------------------------------------------------------
        const EntityId threatened = PickThreatenedSettlement(world, clan);
        const EntityId rally = PickRallyPoint(world, clan);
        bool defenderAssigned = false;

        // A temperament decides how much of the host never leaves home.
        const size_t fieldArmies = static_cast<size_t>(
            static_cast<f32>(clan.cohorts.size()) * (1.0f - m_profile.homeGarrisonShare) + 0.5f);
        size_t campaigning = 0;

        for (EntityId cohortId : clan.cohorts)
        {
            Cohort* cohort = world.FindCohort(cohortId);
            if (!cohort || cohort->IsEmpty() || cohort->inBattle) continue;

            // Plundering is a standing order of this lord's character, not of the moment.
            cohort->mayRaid = m_profile.allowRaiding;

            if (cohort->currentTask.IsMoving()) continue;
            if (cohort->currentTask.type == TaskType::Besiege ||
                cohort->currentTask.type == TaskType::Raid)
            {
                ++campaigning;
                continue;
            }

            // A weakened or unsupplied force goes home before it does anything else.
            if (cohort->supply < 0.3f || world.CohortStrength(cohortId) < 40)
            {
                const Settlement* home = world.NearestSettlement(cohort->position, 1e9f, clan.id);
                if (home && cohort->garrisonOf != home->id)
                {
                    movement.OrderTask(world, cohortId, TaskType::Garrison, home->position, home->id);
                }
                continue;
            }

            if (!defenderAssigned && threatened != kInvalidId)
            {
                const Settlement* settlement = world.FindSettlement(threatened);
                if (settlement && cohort->garrisonOf != threatened)
                {
                    movement.OrderTask(world, cohortId, TaskType::Garrison, settlement->position, threatened);
                    defenderAssigned = true;
                    continue;
                }
            }

            // Anything past the field quota stays home: that is what keeps the host together.
            if (campaigning >= fieldArmies)
            {
                Muster(world, *cohort, rally);
                continue;
            }

            const EntityId target = PickOffensiveTarget(world, clan, realm, cohort->position);
            if (target == kInvalidId)
            {
                // Nothing worth taking within reach: gather on the frontier rather than wander.
                Muster(world, *cohort, rally);
                continue;
            }

            const Settlement* settlement = world.FindSettlement(target);
            if (!settlement) continue;

            // Attack only with the margin this temperament insists on; otherwise plunder, and
            // only if this lord stoops to it at all.
            const f32 power = world.CohortPower(cohortId) * 0.01f;
            const f32 defense = BattleSystem::Get().SettlementDefense(world, target);

            TaskType task = TaskType::Besiege;
            if (power <= defense * m_profile.requiredEdge)
            {
                if (!m_profile.allowRaiding || settlement->owner == kInvalidId) continue;
                task = TaskType::Raid;
            }

            if (movement.OrderTask(world, cohortId, task, settlement->position, target)) ++campaigning;
        }
    }

    // =========================================================================================
    // Diplomacy and expansion
    // =========================================================================================

    f32 AIPlayer::StateStrength(const World& world, EntityId stateId)
    {
        const State* state = world.FindState(stateId);
        if (!state) return 0.0f;

        f32 strength = 0.0f;
        for (EntityId clanId : state->clans)
        {
            const Clan* clan = world.FindClan(clanId);
            if (!clan) continue;
            for (EntityId cohortId : clan->cohorts) strength += world.CohortPower(cohortId);
            for (EntityId settlementId : clan->settlements)
            {
                if (const Settlement* settlement = world.FindSettlement(settlementId))
                    strength += static_cast<f32>(settlement->population) * 0.05f;
            }
        }
        return strength;
    }

    void AIPlayer::ManageDiplomacy(World& world, const Clan& clan, const RealmShape& realm)
    {
        (void)clan;
        const State* self = world.FindState(m_stateId);
        if (!self || !realm.valid) return;

        // One war at a time, and a long breath between them. Without this the AI thinks
        // twice a month and would otherwise be at war with the whole map inside a year.
        const i32 today = world.Time().TotalDays();
        if (today - m_lastWarDay < ConfigManager::Get().Int("ai/warCooldownDays", 900)) return;
        for (const auto& [stateId, other] : world.States())
        {
            if (stateId != m_stateId && self->IsAtWarWith(stateId)) return;
        }
        if (m_random.RangeF(0.0f, 1.0f) > m_profile.warAppetite) return;

        const f32 ownStrength = StateStrength(world, m_stateId);
        if (ownStrength <= 0.0f) return;

        // Only neighbours are worth a war: someone whose land this host can actually reach.
        EntityId victim = kInvalidId;
        f32 bestRatio = m_profile.warStrengthRatio;

        for (const auto& [stateId, other] : world.States())
        {
            if (stateId == m_stateId || other.eliminated) continue;
            if (self->StanceWith(stateId) != DiplomaticStance::Neutral) continue;

            bool adjacent = false;
            for (const auto& [settlementId, settlement] : world.Settlements())
            {
                const Clan* holder = world.FindClan(settlement.owner);
                if (!holder || holder->state != stateId) continue;
                if (WithinReach(realm, settlement.position)) { adjacent = true; break; }
            }
            if (!adjacent) continue;

            const f32 theirStrength = StateStrength(world, stateId);
            const f32 ratio = ownStrength / std::max(1.0f, theirStrength);
            if (ratio > bestRatio) { bestRatio = ratio; victim = stateId; }
        }

        if (victim != kInvalidId)
        {
            DiplomacySystem::Get().DeclareWar(world, m_stateId, victim);
            m_lastWarDay = today;
        }
    }

    void AIPlayer::ManageExpansion(World& world, Clan& clan)
    {
        SettlementSystem& settlements = SettlementSystem::Get();

        if (clan.settlements.empty()) return;
        if (m_profile.expansionWeight <= 0.0f) return;

        // Build outwards from an existing holding: a castle to hold new ground, or a
        // village where the land is good and the treasury is thin.
        const EntityId anchorId = clan.settlements[
            static_cast<size_t>(m_random.Range(0, static_cast<i32>(clan.settlements.size()) - 1))];
        const Settlement* anchor = world.FindSettlement(anchorId);
        if (!anchor) return;

        const SettlementKind kind = clan.resources.CanAfford(
            SettlementDatabase::Get().Kind(SettlementKind::Castle).buildCost)
            ? SettlementKind::Castle
            : SettlementKind::Village;

        // A bolder lord plants his marches further out.
        const f32 range = 220.0f + m_profile.reachBeyondBorder * 0.3f;

        Vec2 site;
        if (!SettlementFactory::FindSite(world, world.Map(), kind, anchor->position, range, m_random,
                                         site, clan.id))
        {
            return;
        }
        settlements.Found(world, clan.id, kind, site);
    }
}
