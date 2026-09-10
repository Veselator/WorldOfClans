// EvaluatorFactory.h - assembles the decorator chain for a settlement.
#pragma once

#include "../Decorators/SettlementDecorators.h"

namespace woc
{
    class MapData;

    /// Builds "base evaluator wrapped in one decorator per standing building".
    /// The chain is cheap to build and is rebuilt whenever a settlement is inspected or
    /// ticked, so a finished construction takes effect immediately.
    class EvaluatorFactory
    {
    public:
        static Scope<ISettlementEvaluator> Build(const Settlement& settlement, const MapData& map);
    };
}
