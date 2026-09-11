#include "Cohort.h"

namespace woc
{
    const char* Task::TypeName(TaskType type)
    {
        switch (type)
        {
        case TaskType::Idle:     return "Стоїть";
        case TaskType::Move:     return "У поході";
        case TaskType::Garrison: return "У гарнізоні";
        case TaskType::Attack:   return "Переслідує";
        case TaskType::Besiege:  return "Облягає";
        case TaskType::Raid:     return "Грабує";
        case TaskType::Patrol:   return "Дозор";
        }
        return "?";
    }

    Json Cohort::ToJson() const
    {
        Json node = Json::MakeObject();
        node["id"] = EncodeId(id);
        node["clan"] = EncodeId(clan);
        node["name"] = name;
        node["x"] = position.x;
        node["y"] = position.y;
        node["experience"] = experience;
        node["supply"] = supply;
        node["mayRaid"] = mayRaid;
        node["suppressRevolts"] = suppressRevolts;
        node["disengage"] = disengageDays;
        node["organisation"] = organisation;
        node["garrisonOf"] = EncodeId(garrisonOf);
        node["units"] = EncodeIdList(units);

        Json task = Json::MakeObject();
        task["type"] = static_cast<i64>(currentTask.type);
        task["x"] = currentTask.destination.x;
        task["y"] = currentTask.destination.y;
        task["targetCohort"] = EncodeId(currentTask.targetCohort);
        task["targetSettlement"] = EncodeId(currentTask.targetSettlement);
        node["task"] = task;
        return node;
    }

    Cohort Cohort::FromJson(const Json& node)
    {
        Cohort cohort;
        cohort.id = DecodeId(node["id"]);
        cohort.clan = DecodeId(node["clan"]);
        cohort.name = node["name"].AsString();
        cohort.position = { node["x"].AsFloat(0.0f), node["y"].AsFloat(0.0f) };
        cohort.experience = node["experience"].AsFloat(0.0f);
        cohort.supply = node["supply"].AsFloat(1.0f);
        cohort.mayRaid = node["mayRaid"].AsBool(false);
        cohort.suppressRevolts = node["suppressRevolts"].AsBool(false);
        cohort.disengageDays = node["disengage"].AsFloat(0.0f);
        cohort.organisation = node["organisation"].AsFloat(1.0f);
        cohort.garrisonOf = DecodeId(node["garrisonOf"]);
        cohort.units = DecodeIdList(node["units"]);

        const Json& task = node["task"];
        cohort.currentTask.type = static_cast<TaskType>(task["type"].AsInt(0));
        cohort.currentTask.destination = { task["x"].AsFloat(0.0f), task["y"].AsFloat(0.0f) };
        cohort.currentTask.targetCohort = DecodeId(task["targetCohort"]);
        cohort.currentTask.targetSettlement = DecodeId(task["targetSettlement"]);
        return cohort;
    }
}
