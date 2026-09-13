#include "GameCommands.h"
#include "Systems/DiplomacySystem.h"

#include "Systems/BattleSystem.h"
#include "Systems/ForestrySystem.h"
#include "Systems/MarketSystem.h"
#include "Systems/MovementSystem.h"
#include "Systems/RoadSystem.h"
#include "Systems/SettlementSystem.h"
#include "World/World.h"
#include "../Core/Log.h"
#include "../Net/NetSession.h"

#include <algorithm>

namespace woc
{
    namespace
    {
        Json Command(const char* verb)
        {
            Json node = Json::MakeObject();
            node["do"] = std::string(verb);
            return node;
        }

        EntityId Id(const Json& node, const char* key)
        {
            return static_cast<EntityId>(node[key].AsNumber(static_cast<f64>(kInvalidId)));
        }

        void PutId(Json& node, const char* key, EntityId id)
        {
            node[key] = static_cast<i64>(id);
        }
    }

    // =========================================================================================
    // Issuing
    // =========================================================================================

    bool GameCommands::Issue(World& world, const Json& command)
    {
        NetSession& session = NetSession::Get();

        // A client changes nothing. It says what its player wants and waits for the world
        // to come back saying whether it happened - which is the only arrangement in which
        // two machines cannot quietly disagree about where an army is.
        if (session.Active())
        {
            session.SubmitOrder(command);
            return false;
        }

        // The host, and a single-player game, run their own orders on the spot. The realm
        // is the local player's by construction, so there is nothing to check.
        return Execute(world, command, std::string());
    }

    EntityId GameCommands::ClanOf(World& world, const Json& command)
    {
        if (const Cohort* cohort = world.FindCohort(Id(command, "cohort"))) return cohort->clan;
        if (const Settlement* settlement = world.FindSettlement(Id(command, "settlement")))
        {
            return settlement->owner;
        }
        if (const Settlement* settlement = world.FindSettlement(Id(command, "from")))
        {
            return settlement->owner;
        }
        if (const MineSite* mine = world.FindMine(Id(command, "mine"))) return mine->owner;

        // Orders that create something from nothing - founding a seat, planting a wood -
        // are simply the acting player's own.
        return kInvalidId;
    }

    bool GameCommands::Execute(World& world, const Json& command, const std::string& peerId)
    {
        const std::string verb = command["do"].AsString();
        if (verb.empty()) return false;

        // --- whose order is this? -------------------------------------------------------
        // On the host, an order from a client may only touch that client's own realm. With
        // no peer named, the order is the local player's and the realm is his by definition.
        Clan* actor = nullptr;
        if (peerId.empty())
        {
            actor = world.HumanClan();
        }
        else
        {
            for (auto& [stateId, state] : world.States())
            {
                if (state.peerId != peerId) continue;
                for (EntityId clanId : state.clans)
                {
                    if (Clan* clan = world.FindClan(clanId)) { actor = clan; break; }
                }
                break;
            }
            if (!actor)
            {
                WOC_LOG_WARN("An order arrived from ", peerId, " who holds no realm");
                return false;
            }

            const EntityId subject = ClanOf(world, command);
            if (subject != kInvalidId)
            {
                const State* theirs = world.StateOfClan(subject);
                if (!theirs || theirs->peerId != peerId)
                {
                    WOC_LOG_WARN("Refused an order from ", peerId, " about somebody else's realm");
                    return false;
                }
            }
        }

        const EntityId actingClan = actor ? actor->id : kInvalidId;

        MovementSystem& movement = MovementSystem::Get();
        SettlementSystem& settlements = SettlementSystem::Get();

        // --- armies ------------------------------------------------------------------------
        if (verb == "task")
        {
            const Vec2 destination{ command["x"].AsFloat(), command["y"].AsFloat() };
            return movement.OrderTask(world, Id(command, "cohort"),
                                      static_cast<TaskType>(command["type"].AsInt(0)),
                                      destination, Id(command, "settlement"),
                                      Id(command, "target"));
        }
        if (verb == "stop")
        {
            Cohort* cohort = world.FindCohort(Id(command, "cohort"));
            if (!cohort) return false;
            cohort->currentTask.Clear();
            return true;
        }
        if (verb == "withdraw") return BattleSystem::Get().Withdraw(world, Id(command, "cohort"));
        if (verb == "disband") return movement.Disband(world, Id(command, "cohort"));
        if (verb == "splitHalf") return movement.SplitInHalf(world, Id(command, "cohort")) != kInvalidId;
        if (verb == "split")
        {
            std::vector<EntityId> units;
            for (const Json& unit : command["units"].AsArray())
            {
                units.push_back(static_cast<EntityId>(unit.AsNumber(0.0)));
            }
            return movement.Split(world, Id(command, "cohort"), units) != kInvalidId;
        }
        if (verb == "merge")
        {
            std::vector<EntityId> cohorts;
            for (const Json& id : command["cohorts"].AsArray())
            {
                cohorts.push_back(static_cast<EntityId>(id.AsNumber(0.0)));
            }
            // Every banner in a merge must answer to the same player.
            for (EntityId id : cohorts)
            {
                const Cohort* cohort = world.FindCohort(id);
                if (!cohort || (actingClan != kInvalidId && cohort->clan != actingClan)) return false;
            }
            return movement.Merge(world, cohorts) != kInvalidId;
        }
        if (verb == "raid" || verb == "suppress")
        {
            Cohort* cohort = world.FindCohort(Id(command, "cohort"));
            if (!cohort) return false;
            const bool value = command["value"].AsBool(false);
            if (verb == "raid")
            {
                cohort->mayRaid = value;
                if (!value && cohort->currentTask.type == TaskType::Raid) cohort->currentTask.Clear();
            }
            else
            {
                cohort->suppressRevolts = value;
                if (!value && cohort->currentTask.type == TaskType::Attack) cohort->currentTask.Clear();
            }
            return true;
        }

        // --- settlements ----------------------------------------------------------------------
        if (verb == "build")
        {
            return settlements.StartConstruction(world, Id(command, "settlement"),
                                                 command["building"].AsString());
        }
        if (verb == "cancelRecruit")
        {
            return settlements.CancelRecruit(world, Id(command, "settlement"),
                                             static_cast<size_t>(std::max(0, command["index"].AsInt(0))));
        }
        if (verb == "cancelBuild")
        {
            return settlements.CancelConstruction(world, Id(command, "settlement"),
                                                  command["building"].AsString());
        }
        if (verb == "recruit")
        {
            return settlements.Recruit(world, Id(command, "settlement"), Id(command, "cohort"),
                                       static_cast<UnitRole>(command["role"].AsInt(0))) != kInvalidId;
        }
        if (verb == "road")
        {
            const Settlement* from = world.FindSettlement(Id(command, "from"));
            if (!from) return false;
            const RoadPlan plan = RoadSystem::Get().Plan(world, from->id, Id(command, "to"));
            return RoadSystem::Get().Begin(world, from->owner, plan);
        }
        if (verb == "convert")
        {
            return settlements.StartConversion(world, Id(command, "settlement"),
                                               command["faith"].AsString());
        }
        if (verb == "raze")
        {
            Settlement* settlement = world.FindSettlement(Id(command, "settlement"));
            if (!settlement) return false;
            return settlements.Raze(world, settlement->id, settlement->owner);
        }
        if (verb == "independence")
        {
            return settlements.GrantIndependence(world, Id(command, "settlement"));
        }
        if (verb == "mine")
        {
            if (actingClan == kInvalidId) return false;
            return settlements.DevelopMine(world, actingClan, Id(command, "mine"));
        }
        if (verb == "found")
        {
            if (actingClan == kInvalidId) return false;
            const Vec2 position{ command["x"].AsFloat(), command["y"].AsFloat() };
            return settlements.Found(world, actingClan,
                                     static_cast<SettlementKind>(command["kind"].AsInt(0)),
                                     position, command["name"].AsString()) != kInvalidId;
        }
        if (verb == "plant")
        {
            if (actingClan == kInvalidId) return false;
            ForestrySystem& forestry = ForestrySystem::Get();
            const Vec2 position{ command["x"].AsFloat(), command["y"].AsFloat() };
            const PlantingPlan plan = forestry.PlanPlanting(world, actingClan, position);
            return plan.valid && forestry.BeginPlanting(world, actingClan, plan);
        }
        if (verb == "diplomacy" || verb == "answerOffer")
        {
            const State* self = actor ? world.StateOfClan(actor->id) : nullptr;
            if (!self) return false;
            DiplomacySystem& diplomacy = DiplomacySystem::Get();
            if (verb == "diplomacy")
            {
                return diplomacy.Perform(world, self->id, Id(command, "state"),
                                         static_cast<DiplomaticAction::Kind>(command["kind"].AsInt(0)));
            }
            return command["accept"].AsBool(false) ? diplomacy.AcceptOffer(world, self->id)
                                                   : diplomacy.DeclineOffer(world, self->id);
        }
        if (verb == "trade")
        {
            const Settlement* settlement = world.FindSettlement(Id(command, "settlement"));
            if (!settlement) return false;
            return MarketSystem::Get().Trade(world, settlement->id, settlement->owner,
                                             static_cast<ResourceType>(command["give"].AsInt(0)),
                                             static_cast<ResourceType>(command["take"].AsInt(0)),
                                             command["amount"].AsFloat()) > 0.0f;
        }

        WOC_LOG_WARN("Unknown order: ", verb);
        return false;
    }

    // =========================================================================================
    // The orders themselves
    // =========================================================================================

    Json GameCommands::Task(EntityId cohort, TaskType type, const Vec2& destination,
                            EntityId settlement, EntityId targetCohort)
    {
        Json node = Command("task");
        PutId(node, "cohort", cohort);
        node["type"] = static_cast<i64>(type);
        node["x"] = destination.x;
        node["y"] = destination.y;
        PutId(node, "settlement", settlement);
        PutId(node, "target", targetCohort);
        return node;
    }

    Json GameCommands::Diplomacy(EntityId targetState, i32 actionKind)
    {
        Json node = Command("diplomacy");
        PutId(node, "state", targetState);
        node["kind"] = actionKind;
        return node;
    }

    Json GameCommands::AnswerOffer(bool accept)
    {
        Json node = Command("answerOffer");
        node["accept"] = accept;
        return node;
    }

    Json GameCommands::Stop(EntityId cohort)
    {
        Json node = Command("stop");
        PutId(node, "cohort", cohort);
        return node;
    }

    Json GameCommands::Withdraw(EntityId cohort)
    {
        Json node = Command("withdraw");
        PutId(node, "cohort", cohort);
        return node;
    }

    Json GameCommands::Disband(EntityId cohort)
    {
        Json node = Command("disband");
        PutId(node, "cohort", cohort);
        return node;
    }

    Json GameCommands::Split(EntityId cohort, const std::vector<EntityId>& units)
    {
        Json node = Command("split");
        PutId(node, "cohort", cohort);
        Json list = Json::MakeArray();
        for (EntityId unit : units) list.Push(static_cast<i64>(unit));
        node["units"] = list;
        return node;
    }

    Json GameCommands::SplitInHalf(EntityId cohort)
    {
        Json node = Command("splitHalf");
        PutId(node, "cohort", cohort);
        return node;
    }

    Json GameCommands::Merge(const std::vector<EntityId>& cohorts)
    {
        Json node = Command("merge");
        Json list = Json::MakeArray();
        for (EntityId id : cohorts) list.Push(static_cast<i64>(id));
        node["cohorts"] = list;
        return node;
    }

    Json GameCommands::SetRaiding(EntityId cohort, bool value)
    {
        Json node = Command("raid");
        PutId(node, "cohort", cohort);
        node["value"] = value;
        return node;
    }

    Json GameCommands::SetSuppressing(EntityId cohort, bool value)
    {
        Json node = Command("suppress");
        PutId(node, "cohort", cohort);
        node["value"] = value;
        return node;
    }

    Json GameCommands::Build(EntityId settlement, const std::string& building)
    {
        Json node = Command("build");
        PutId(node, "settlement", settlement);
        node["building"] = building;
        return node;
    }

    Json GameCommands::CancelBuild(EntityId settlement, const std::string& building)
    {
        Json node = Command("cancelBuild");
        PutId(node, "settlement", settlement);
        node["building"] = building;
        return node;
    }

    Json GameCommands::CancelRecruit(EntityId settlement, i32 index)
    {
        Json node = Command("cancelRecruit");
        PutId(node, "settlement", settlement);
        node["index"] = static_cast<i64>(index);
        return node;
    }

    Json GameCommands::Recruit(EntityId settlement, EntityId cohort, UnitRole role)
    {
        Json node = Command("recruit");
        PutId(node, "settlement", settlement);
        PutId(node, "cohort", cohort);
        node["role"] = static_cast<i64>(role);
        return node;
    }

    Json GameCommands::Road(EntityId from, EntityId to)
    {
        Json node = Command("road");
        PutId(node, "from", from);
        PutId(node, "to", to);
        return node;
    }

    Json GameCommands::Convert(EntityId settlement, const std::string& faith)
    {
        Json node = Command("convert");
        PutId(node, "settlement", settlement);
        node["faith"] = faith;
        return node;
    }

    Json GameCommands::Raze(EntityId settlement)
    {
        Json node = Command("raze");
        PutId(node, "settlement", settlement);
        return node;
    }

    Json GameCommands::Independence(EntityId settlement)
    {
        Json node = Command("independence");
        PutId(node, "settlement", settlement);
        return node;
    }

    Json GameCommands::DevelopMine(EntityId mine)
    {
        Json node = Command("mine");
        PutId(node, "mine", mine);
        return node;
    }

    Json GameCommands::Found(SettlementKind kind, const Vec2& position, const std::string& name)
    {
        Json node = Command("found");
        node["kind"] = static_cast<i64>(kind);
        node["x"] = position.x;
        node["y"] = position.y;
        node["name"] = name;
        return node;
    }

    Json GameCommands::PlantForest(const Vec2& position)
    {
        Json node = Command("plant");
        node["x"] = position.x;
        node["y"] = position.y;
        return node;
    }

    Json GameCommands::Trade(EntityId settlement, i32 give, i32 take, f32 amount)
    {
        Json node = Command("trade");
        PutId(node, "settlement", settlement);
        node["give"] = give;
        node["take"] = take;
        node["amount"] = amount;
        return node;
    }
}
