// EntityJson.h - shared helpers for round-tripping entity ids through JSON.
#pragma once

#include "../../Core/Json.h"

namespace woc
{
    /// Entity ids are unsigned; "none" is written as -1 so saved files stay readable and
    /// the value survives the signed conversion on the way back in.
    inline i64 EncodeId(EntityId id) { return id == kInvalidId ? -1 : static_cast<i64>(id); }

    inline EntityId DecodeId(const Json& node)
    {
        const f64 value = node.AsNumber(-1.0);
        return value < 0.0 ? kInvalidId : static_cast<EntityId>(value);
    }

    inline Json EncodeIdList(const std::vector<EntityId>& ids)
    {
        Json list = Json::MakeArray();
        for (EntityId id : ids) list.Push(EncodeId(id));
        return list;
    }

    inline std::vector<EntityId> DecodeIdList(const Json& node)
    {
        std::vector<EntityId> ids;
        for (const Json& entry : node.AsArray()) ids.push_back(DecodeId(entry));
        return ids;
    }
}
