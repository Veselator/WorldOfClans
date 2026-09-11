#include "DiplomacySystem.h"
#include "../Factories/CharacterFactory.h"
#include "../World/World.h"
#include "../../Core/Config.h"
#include "../../Core/Random.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        /// The leading clan of a realm, which is who actually marries and signs.
        Clan* LeadClan(World& world, EntityId stateId)
        {
            State* state = world.FindState(stateId);
            return state ? world.FindClan(state->leader) : nullptr;
        }

        const Clan* LeadClan(const World& world, EntityId stateId)
        {
            const State* state = world.FindState(stateId);
            return state ? world.FindClan(state->leader) : nullptr;
        }
    }

    void DiplomacySystem::SetStance(World& world, EntityId a, EntityId b, DiplomaticStance stance, i32 untilDay)
    {
        State* stateA = world.FindState(a);
        State* stateB = world.FindState(b);
        if (!stateA || !stateB) return;

        stateA->RelationWith(b).stance = stance;
        stateA->RelationWith(b).stanceUntilDay = untilDay;
        stateB->RelationWith(a).stance = stance;
        stateB->RelationWith(a).stanceUntilDay = untilDay;
    }

    void DiplomacySystem::AdjustOpinion(World& world, EntityId a, EntityId b, f32 delta)
    {
        State* stateA = world.FindState(a);
        State* stateB = world.FindState(b);
        if (!stateA || !stateB) return;

        stateA->RelationWith(b).opinion = std::clamp(stateA->RelationWith(b).opinion + delta, -100.0f, 100.0f);
        stateB->RelationWith(a).opinion = std::clamp(stateB->RelationWith(a).opinion + delta, -100.0f, 100.0f);
    }

    f32 DiplomacySystem::Opinion(const World& world, EntityId from, EntityId to) const
    {
        const State* state = world.FindState(to);
        if (!state) return 0.0f;

        f32 opinion = state->RelationWith(from).opinion;
        for (const auto& [reason, value] : OpinionBreakdown(world, from, to)) opinion += value;
        return std::clamp(opinion, -100.0f, 100.0f);
    }

    std::vector<std::pair<std::string, f32>> DiplomacySystem::OpinionBreakdown(const World& world,
                                                                              EntityId from, EntityId to) const
    {
        std::vector<std::pair<std::string, f32>> parts;
        const State* a = world.FindState(from);
        const State* b = world.FindState(to);
        if (!a || !b) return parts;

        ConfigManager& config = ConfigManager::Get();

        if (a->raceId == b->raceId)
        {
            parts.emplace_back("Один народ", config.Float("diplomacy/sameRaceOpinion", 12.0f));
        }

        const Clan* clanA = LeadClan(world, from);
        const Clan* clanB = LeadClan(world, to);
        if (clanA && clanB && clanA->faithId == clanB->faithId)
        {
            parts.emplace_back("Одна віра", config.Float("diplomacy/sameFaithOpinion", 10.0f));
        }

        // Realms whose borders touch have more to argue about.
        if (clanA && clanB)
        {
            f32 closest = 1e9f;
            for (EntityId settlementA : clanA->settlements)
            {
                const Settlement* sa = world.FindSettlement(settlementA);
                if (!sa) continue;
                for (EntityId settlementB : clanB->settlements)
                {
                    const Settlement* sb = world.FindSettlement(settlementB);
                    if (!sb) continue;
                    closest = std::min(closest, Distance(sa->position, sb->position));
                }
            }
            if (closest < 260.0f)
            {
                parts.emplace_back("Спірний кордон", config.Float("diplomacy/borderFrictionOpinion", -14.0f));
            }
        }

        if (b->StanceWith(from) == DiplomaticStance::Alliance)
        {
            parts.emplace_back("Союзники", 20.0f);
        }
        return parts;
    }

    std::vector<DiplomaticAction> DiplomacySystem::AvailableActions(World& world, EntityId from, EntityId to) const
    {
        std::vector<DiplomaticAction> actions;
        const State* a = world.FindState(from);
        const State* b = world.FindState(to);
        if (!a || !b || from == to) return actions;

        const DiplomaticStance stance = a->StanceWith(to);
        const f32 opinion = Opinion(world, from, to);
        ConfigManager& config = ConfigManager::Get();

        if (stance == DiplomaticStance::War)
        {
            DiplomaticAction peace;
            peace.kind = DiplomaticAction::Kind::OfferPeace;
            peace.label = "Запропонувати мир";
            peace.tooltip = "Припинити війну й укласти перемир'я";
            peace.available = opinion > -70.0f;
            actions.push_back(peace);
        }
        else
        {
            DiplomaticAction war;
            war.kind = DiplomaticAction::Kind::DeclareWar;
            war.label = "Оголосити війну";
            war.tooltip = "Розірвати будь-які договори й почати війну";
            war.available = stance != DiplomaticStance::Truce;
            actions.push_back(war);

            if (stance == DiplomaticStance::Neutral || stance == DiplomaticStance::Truce)
            {
                DiplomaticAction pact;
                pact.kind = DiplomaticAction::Kind::NonAggression;
                pact.label = "Пакт про ненапад";
                pact.tooltip = "Зобов'язання не воювати";
                pact.available = opinion > 0.0f;
                actions.push_back(pact);
            }

            if (stance == DiplomaticStance::NonAggression)
            {
                DiplomaticAction alliance;
                alliance.kind = DiplomaticAction::Kind::Alliance;
                alliance.label = "Укласти союз";
                alliance.tooltip = "Воювати разом проти спільних ворогів";
                alliance.available = opinion >= config.Float("diplomacy/allianceOpinionThreshold", 45.0f);
                actions.push_back(alliance);
            }

            DiplomaticAction marriage;
            marriage.kind = DiplomaticAction::Kind::ArrangeMarriage;
            marriage.label = "Влаштувати шлюб";
            marriage.tooltip = "Скріпити союз родинними зв'язками";
            marriage.available = opinion > -20.0f;
            actions.push_back(marriage);
        }
        return actions;
    }

    bool DiplomacySystem::Perform(World& world, EntityId from, EntityId to, DiplomaticAction::Kind kind)
    {
        switch (kind)
        {
        case DiplomaticAction::Kind::DeclareWar:      DeclareWar(world, from, to); return true;
        case DiplomaticAction::Kind::OfferPeace:      MakePeace(world, from, to); return true;
        case DiplomaticAction::Kind::NonAggression:   SignNonAggression(world, from, to); return true;
        case DiplomaticAction::Kind::Alliance:        FormAlliance(world, from, to); return true;
        case DiplomaticAction::Kind::ArrangeMarriage: return ArrangeMarriage(world, from, to);
        case DiplomaticAction::Kind::BreakPact:       SetStance(world, from, to, DiplomaticStance::Neutral, 0);
                                                      AdjustOpinion(world, from, to, -25.0f);
                                                      return true;
        }
        return false;
    }

    void DiplomacySystem::DeclareWar(World& world, EntityId from, EntityId to)
    {
        SetStance(world, from, to, DiplomaticStance::War, 0);
        AdjustOpinion(world, from, to, -40.0f);

        const State* a = world.FindState(from);
        const State* b = world.FindState(to);
        if (a && b)
        {
            world.Log(a->name + " оголошує війну державі " + b->name, Color::FromRGB(0xC05046));

            // A war the player is in is not a line in a corner. It is shouted.
            if (a->playerControlled || b->playerControlled)
            {
                const bool ours = a->playerControlled;
                world.Announce(ours ? "ВІЙНА ОГОЛОШЕНА" : "НАМ ОГОЛОСИЛИ ВІЙНУ",
                               ours ? "Ви йдете війною на державу " + b->name
                                    : a->name + " іде війною на вас",
                               Color::FromRGB(0xD8342A));
            }
        }
        world.Flare(from, to, Color::FromRGB(0xD8342A));
    }

    void DiplomacySystem::MakePeace(World& world, EntityId from, EntityId to)
    {
        const i32 truceDays = ConfigManager::Get().Int("diplomacy/truceDays", 360);
        SetStance(world, from, to, DiplomaticStance::Truce, world.Time().TotalDays() + truceDays);
        AdjustOpinion(world, from, to, 15.0f);

        // Sieges are lifted when the war ends.
        for (auto& [id, cohort] : world.Cohorts())
        {
            if (cohort.currentTask.type == TaskType::Besiege || cohort.currentTask.type == TaskType::Raid)
            {
                const Settlement* target = world.FindSettlement(cohort.currentTask.targetSettlement);
                if (!target) continue;
                const State* targetState = world.StateOfClan(target->owner);
                const State* actorState = world.StateOfClan(cohort.clan);
                if (!targetState || !actorState) continue;
                if ((actorState->id == from && targetState->id == to) ||
                    (actorState->id == to && targetState->id == from))
                {
                    cohort.currentTask.Clear();
                }
            }
        }

        const State* a = world.FindState(from);
        const State* b = world.FindState(to);
        if (a && b) world.Log(a->name + " і " + b->name + " укладають перемир'я", Color::FromRGB(0x5AA860));
    }

    void DiplomacySystem::SignNonAggression(World& world, EntityId from, EntityId to)
    {
        SetStance(world, from, to, DiplomaticStance::NonAggression, 0);
        AdjustOpinion(world, from, to, ConfigManager::Get().Float("diplomacy/nonAggressionOpinionGain", 15.0f));

        const State* a = world.FindState(from);
        const State* b = world.FindState(to);
        if (a && b) world.Log(a->name + " і " + b->name + " підписують пакт про ненапад", Color::FromRGB(0x5AA860));
    }

    void DiplomacySystem::FormAlliance(World& world, EntityId from, EntityId to)
    {
        SetStance(world, from, to, DiplomaticStance::Alliance, 0);
        AdjustOpinion(world, from, to, 20.0f);

        const State* a = world.FindState(from);
        const State* b = world.FindState(to);
        if (a && b) world.Log(a->name + " і " + b->name + " укладають союз", Color::FromRGB(0xC9A227));
        world.Flare(from, to, Color::FromRGB(0x3F8FE0));
    }

    bool DiplomacySystem::ArrangeMarriage(World& world, EntityId from, EntityId to)
    {
        Clan* clanA = LeadClan(world, from);
        Clan* clanB = LeadClan(world, to);
        if (!clanA || !clanB) return false;

        ConfigManager& config = ConfigManager::Get();
        const i32 marriageAge = config.Int("characters/marriageAge", 18);
        const i32 maxAge = config.Int("characters/maxAge", 92);

        // Find one unmarried adult in each house, of opposite gender.
        auto findCandidate = [&](Clan& clan, Gender gender) -> Character*
        {
            for (EntityId memberId : clan.members)
            {
                Character* member = world.FindCharacter(memberId);
                if (!member || !member->alive) continue;
                if (member->gender != gender) continue;
                if (member->spouse != kInvalidId) continue;
                if (member->age < marriageAge || member->age > maxAge - 20) continue;
                return member;
            }
            return nullptr;
        };

        Character* groom = findCandidate(*clanA, Gender::Male);
        Character* bride = findCandidate(*clanB, Gender::Female);
        if (!groom || !bride)
        {
            groom = findCandidate(*clanB, Gender::Male);
            bride = findCandidate(*clanA, Gender::Female);
        }
        if (!groom || !bride) return false;

        groom->spouse = bride->id;
        bride->spouse = groom->id;

        AdjustOpinion(world, from, to, config.Float("diplomacy/marriageOpinionGain", 30.0f));

        // A wedding between houses at war is, in practice, a peace treaty.
        const State* fromState = world.FindState(from);
        if (fromState && fromState->StanceWith(to) == DiplomaticStance::War)
        {
            MakePeace(world, from, to);
        }

        world.Log(groom->FullName() + " бере за дружину " + bride->FullName(), Color::FromRGB(0xC9A227));
        world.Flare(from, to, Color::FromRGB(0x4CC46A));
        return true;
    }

    void DiplomacySystem::Tick(World& world)
    {
        ConfigManager& config = ConfigManager::Get();
        const f32 drift = config.Float("diplomacy/opinionDriftPerMonth", 0.5f);
        const f32 warThreshold = config.Float("diplomacy/warOpinionThreshold", -35.0f);
        const i32 today = world.Time().TotalDays();

        std::vector<EntityId> stateIds;
        for (const auto& [id, state] : world.States())
        {
            if (!state.eliminated) stateIds.push_back(id);
        }
        std::sort(stateIds.begin(), stateIds.end());

        // Truces lapse, and raw opinion creeps back towards indifference.
        for (EntityId id : stateIds)
        {
            State* state = world.FindState(id);
            if (!state) continue;
            for (auto& [other, relation] : state->relations)
            {
                if (relation.stance == DiplomaticStance::Truce && today >= relation.stanceUntilDay)
                {
                    relation.stance = DiplomaticStance::Neutral;
                }
                relation.opinion -= std::clamp(relation.opinion, -drift, drift);
            }
        }

        // The AI looks for a casus belli against a neighbour it already dislikes.
        Random& random = GlobalRandom();
        for (EntityId id : stateIds)
        {
            State* state = world.FindState(id);
            if (!state || state->playerControlled) continue;

            for (EntityId other : stateIds)
            {
                if (other == id) continue;
                State* target = world.FindState(other);
                if (!target) continue;

                const DiplomaticStance stance = state->StanceWith(other);
                const f32 opinion = Opinion(world, other, id);

                if (stance == DiplomaticStance::Neutral && opinion < warThreshold &&
                    random.Chance(config.Float("ai/aggression", 0.55f) * 0.05f))
                {
                    DeclareWar(world, id, other);
                }
                else if (stance == DiplomaticStance::Neutral && opinion > 30.0f && random.Chance(0.04f))
                {
                    // Between two AIs a pact is simply signed; with the player it is asked.
                    if (target->playerControlled) Propose(world, id, other, DiplomaticAction::Kind::NonAggression);
                    else SignNonAggression(world, id, other);
                }
                else if (stance == DiplomaticStance::NonAggression && opinion > 55.0f && random.Chance(0.02f))
                {
                    if (target->playerControlled) Propose(world, id, other, DiplomaticAction::Kind::Alliance);
                    else FormAlliance(world, id, other);
                }
                else if (stance != DiplomaticStance::War && opinion > 45.0f && random.Chance(0.015f))
                {
                    // A match between the two houses: offered to the player, arranged with
                    // anyone else.
                    if (target->playerControlled) Propose(world, id, other, DiplomaticAction::Kind::ArrangeMarriage);
                    else ArrangeMarriage(world, id, other);
                }
                else if (stance == DiplomaticStance::War && opinion > -10.0f && random.Chance(0.06f))
                {
                    MakePeace(world, id, other);
                }
            }
        }
    }

    // =====================================================================================
    // Offers waiting on the player
    // =====================================================================================

    const char* DiplomacySystem::OfferTitle(DiplomaticAction::Kind kind)
    {
        switch (kind)
        {
        case DiplomaticAction::Kind::NonAggression:  return "ПРОПОЗИЦІЯ ПАКТУ";
        case DiplomaticAction::Kind::Alliance:       return "ПРОПОЗИЦІЯ СОЮЗУ";
        case DiplomaticAction::Kind::ArrangeMarriage: return "ПРОПОЗИЦІЯ ШЛЮБУ";
        case DiplomaticAction::Kind::OfferPeace:     return "ПРОПОЗИЦІЯ МИРУ";
        default:                                     return "ПОСОЛЬСТВО";
        }
    }

    const char* DiplomacySystem::OfferBody(DiplomaticAction::Kind kind)
    {
        switch (kind)
        {
        case DiplomaticAction::Kind::NonAggression:
            return "пропонує пакт про ненапад. Жодна зі сторін не підніме меча на іншу, "
                   "поки пакт не буде розірвано.";
        case DiplomaticAction::Kind::Alliance:
            return "пропонує оборонний союз. Кожен стає на захист іншого, коли на того "
                   "нападуть.";
        case DiplomaticAction::Kind::ArrangeMarriage:
            return "пропонує шлюб між домами. Родичання вабить обидва двори одне до одного "
                   "надовго.";
        case DiplomaticAction::Kind::OfferPeace:
            return "пропонує скінчити війну й розійтися з перемир'ям.";
        default:
            return "шле посольство.";
        }
    }

    void DiplomacySystem::Propose(World& world, EntityId from, EntityId to, DiplomaticAction::Kind kind)
    {
        // One embassy at a time about one thing: an AI that rolls the same offer twice in a
        // month should not stack two identical dialogs on the player.
        for (const DiplomaticOffer& waiting : m_offers)
        {
            if (waiting.from == from && waiting.to == to && waiting.kind == kind) return;
        }
        m_offers.push_back({ from, to, kind, world.Time().TotalDays() });
    }

    void DiplomacySystem::AcceptOffer(World& world)
    {
        if (m_offers.empty()) return;
        const DiplomaticOffer offer = m_offers.front();
        m_offers.pop_front();

        switch (offer.kind)
        {
        case DiplomaticAction::Kind::NonAggression:   SignNonAggression(world, offer.from, offer.to); break;
        case DiplomaticAction::Kind::Alliance:        FormAlliance(world, offer.from, offer.to); break;
        case DiplomaticAction::Kind::ArrangeMarriage: ArrangeMarriage(world, offer.from, offer.to); break;
        case DiplomaticAction::Kind::OfferPeace:      MakePeace(world, offer.from, offer.to); break;
        default: break;
        }
    }

    void DiplomacySystem::DeclineOffer(World& world)
    {
        if (m_offers.empty()) return;
        const DiplomaticOffer offer = m_offers.front();
        m_offers.pop_front();

        // A refusal is not an insult, but it is remembered.
        AdjustOpinion(world, offer.to, offer.from,
                      -ConfigManager::Get().Float("diplomacy/refusalOpinion", 8.0f));

        const State* asker = world.FindState(offer.from);
        if (asker) world.Log("Відмовлено посольству держави " + asker->name, Color::FromRGB(0x9AA3AB));
    }
}
