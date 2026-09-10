#include "EvaluatorFactory.h"
#include "../World/BuildingDatabase.h"

namespace woc
{
    Scope<ISettlementEvaluator> EvaluatorFactory::Build(const Settlement& settlement, const MapData& map)
    {
        Scope<ISettlementEvaluator> chain = MakeScope<SettlementBaseEvaluator>(settlement, map);

        const BuildingDatabase& buildings = BuildingDatabase::Get();
        for (const std::string& buildingId : settlement.buildings)
        {
            const BuildingInfo* info = buildings.Find(buildingId);
            if (!info) continue;

            switch (info->decorator)
            {
            case DecoratorKind::Production:
                chain = MakeScope<ProductionDecorator>(std::move(chain), *info);
                break;
            case DecoratorKind::Defense:
                chain = MakeScope<DefenseDecorator>(std::move(chain), *info);
                break;
            case DecoratorKind::Military:
                chain = MakeScope<MilitaryDecorator>(std::move(chain), *info);
                break;
            case DecoratorKind::Loyalty:
                chain = MakeScope<LoyaltyDecorator>(std::move(chain), *info);
                break;
            case DecoratorKind::Coverage:
                chain = MakeScope<CoverageDecorator>(std::move(chain), *info);
                break;
            }
        }
        return chain;
    }
}
