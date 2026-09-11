#include "SettlementFactory.h"
#include "../Systems/CoverageSystem.h"
#include "NamePool.h"
#include "../World/World.h"
#include "../World/RaceDatabase.h"
#include "../../Core/Config.h"

#include <algorithm>

namespace woc
{
    Settlement& SettlementFactory::Create(World& world, const SettlementRequest& request, Random& random)
    {
        const SettlementDatabase& db = SettlementDatabase::Get();
        const RaceInfo& race = RaceDatabase::Get().Race(request.raceId);

        Settlement& settlement = world.CreateSettlement();
        settlement.kind = request.kind;
        settlement.position = request.position;
        settlement.raceId = request.raceId;
        settlement.faithId = request.faithId.empty() ? race.defaultFaith : request.faithId;
        settlement.owner = request.owner;
        settlement.name = request.name.empty()
            ? NamePool::Get().SettlementName(request.raceId, random)
            : request.name;

        if (request.population > 0)
        {
            settlement.population = request.population;
        }
        else
        {
            switch (request.kind)
            {
            case SettlementKind::Village: settlement.population = random.Range(120, 520); break;
            case SettlementKind::City:    settlement.population = random.Range(1400, 3400); break;
            default:                      settlement.population = random.Range(300, 700); break;
            }
        }

        settlement.prosperity = settlement.ProsperityCeiling() *
            std::clamp(db.StartingProsperity() * random.RangeF(0.8f, 1.2f), 0.05f, 1.2f);
        settlement.loyalty = std::clamp(db.StartingLoyalty() * random.RangeF(0.9f, 1.1f), 0.05f, 1.0f);

        if (Clan* clan = world.FindClan(request.owner)) clan->AddSettlement(settlement.id);
        return settlement;
    }

    bool SettlementFactory::CanPlace(const World& world, const MapData& map, SettlementKind kind,
                                     const Vec2& position, EntityId clanId)
    {
        if (position.x < 4.0f || position.y < 4.0f ||
            position.x >= static_cast<f32>(map.PixelWidth()) - 4.0f ||
            position.y >= static_cast<f32>(map.PixelHeight()) - 4.0f)
        {
            return false;
        }

        const Coord tile = map.ToTile(position);
        if (!map.IsBuildable(tile)) return false;

        const SettlementDatabase& db = SettlementDatabase::Get();
        const f32 minimum = kind == SettlementKind::Village ? db.MinDistanceVillage() : db.MinDistanceBetween();
        const f32 minimumSq = minimum * minimum;

        for (const auto& [id, other] : world.Settlements())
        {
            const f32 required = (other.kind == SettlementKind::Village && kind == SettlementKind::Village)
                ? minimumSq
                : db.MinDistanceBetween() * db.MinDistanceBetween();
            if (DistanceSq(other.position, position) < required) return false;
        }

        // A clan builds on its own ground and nowhere else. Whether that ground is its own
        // is the coverage field's answer, so the borders a player can see are exactly the
        // borders he can build inside.
        if (clanId != kInvalidId && CoverageSystem::Get().OwnerAt(world, position) != clanId)
        {
            return false;
        }
        return true;
    }

    bool SettlementFactory::FindSite(const World& world, const MapData& map, SettlementKind kind,
                                     const Vec2& origin, f32 searchRadius, Random& random,
                                     Vec2& outPosition, EntityId clanId)
    {
        // Sample rings outwards so new settlements hug their parent rather than teleporting.
        for (int attempt = 0; attempt < 160; ++attempt)
        {
            const f32 angle = random.RangeF(0.0f, 2.0f * kPi);
            const f32 radius = searchRadius * std::sqrt(random.Unit());
            const Vec2 candidate{ origin.x + std::cos(angle) * radius, origin.y + std::sin(angle) * radius };
            if (!CanPlace(world, map, kind, candidate, clanId)) continue;

            outPosition = candidate;
            return true;
        }
        return false;
    }
}
