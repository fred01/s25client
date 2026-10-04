// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "world/BuildingRanges.h"
#include "GamePlayer.h"
#include "RttrForeachPt.h"
#include "buildings/noBaseBuilding.h"
#include "buildings/nobMilitary.h"
#include "figures/nofFarmhand.h"
#include "world/GameWorldBase.h"
#include "world/MapGeometry.h"
#include "world/TerritoryRegion.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/GameConsts.h"
#include "gameData/MilitaryConsts.h"
#include <algorithm>

namespace {
unsigned getMilitarySize(const BuildingType bldType)
{
    switch(bldType)
    {
        case BuildingType::Barracks: return 0;
        case BuildingType::Guardhouse: return 1;
        case BuildingType::Watchtower: return 2;
        default: RTTR_Assert(bldType == BuildingType::Fortress); return 3;
    }
}

/// Additional radius used to find territory changes, same as for real territory recalculations
constexpr unsigned TERRITORY_ADD_RADIUS = 2;
} // namespace

BuildingRanges::BuildingRanges(const GameWorldBase& world, const noBaseBuilding& building) : world(world)
{
    const BuildingType bldType = building.GetBuildingType();
    const MapPoint pos = building.GetPos();
    const bool isSite = building.GetGOT() == GO_Type::Buildingsite;

    if(BuildingProperties::IsMilitary(bldType))
    {
        const unsigned size = getMilitarySize(bldType);
        const auto* milBld = isSite ? nullptr : &static_cast<const nobMilitary&>(building);
        // Not yet occupied buildings show what they will do once they are fully occupied
        const bool isPlanned = !milBld || milBld->IsNewBuilt();
        const unsigned attackRadius =
          isPlanned ? nobMilitary::GetMaxAttackDistance(nobMilitary::GetNumAttackersOfTroops(
                        NUM_TROOPS[building.GetNation()][size], world.GetPlayer(building.GetPlayer()))) :
                      milBld->GetMaxAttackDistance();
        AddRange(pos, MILITARY_RADIUS[size], BuildingRangeKind::Territory);
        AddRange(pos, MAX_AGGRESSIVE_DEFENDER_DISTANCE, BuildingRangeKind::Defense);
        AddRange(pos, attackRadius, BuildingRangeKind::Attack);
        CalcTerritoryChange(building, isPlanned ? MILITARY_RADIUS[size] : 0);
    } else
    {
        switch(bldType)
        {
            case BuildingType::Headquarters:
                AddRange(pos, HQ_RADIUS, BuildingRangeKind::Territory);
                CalcTerritoryChange(building, 0);
                break;
            case BuildingType::HarborBuilding:
                AddRange(pos, HARBOR_RADIUS, BuildingRangeKind::Territory);
                AddRange(pos, MAX_AGGRESSIVE_DEFENDER_DISTANCE, BuildingRangeKind::Defense);
                CalcTerritoryChange(building, isSite ? HARBOR_RADIUS : 0);
                break;
            case BuildingType::Catapult: AddRange(pos, CATAPULT_RANGE, BuildingRangeKind::Attack); break;
            case BuildingType::LookoutTower:
                AddRange(pos, VISUALRANGE_LOOKOUTTOWER, BuildingRangeKind::Work);
                break;
            case BuildingType::Woodcutter:
            case BuildingType::Forester:
            case BuildingType::Quarry:
            case BuildingType::Fishery:
            case BuildingType::Charburner:
            case BuildingType::Farm:
            case BuildingType::Vineyard:
                AddRange(pos, nofFarmhand::GetWorkRadius(*BLD_WORK_DESC[bldType].job, world.GetGGS()),
                         BuildingRangeKind::Work);
                break;
            case BuildingType::Hunter: AddRange(pos, HUNTER_SEARCH_RADIUS, BuildingRangeKind::Work); break;
            case BuildingType::Skinner: AddRange(pos, SKINNER_SEARCH_RADIUS, BuildingRangeKind::Work); break;
            case BuildingType::GraniteMine:
            case BuildingType::CoalMine:
            case BuildingType::IronMine:
            case BuildingType::GoldMine:
            case BuildingType::Well: AddRange(pos, MINER_RADIUS, BuildingRangeKind::Work); break;
            case BuildingType::Shipyard:
                AddRange(building.GetFlagPos(), SHIPWRIGHT_RADIUS, BuildingRangeKind::Work);
                break;
            default: break;
        }
    }

    std::stable_sort(ranges.begin(), ranges.end(),
                     [](const Range& lhs, const Range& rhs) { return lhs.kind > rhs.kind; });
}

void BuildingRanges::AddRange(const MapPoint center, const unsigned radius, const BuildingRangeKind kind)
{
    if(radius > 0)
        ranges.push_back(Range{center, radius, kind});
}

void BuildingRanges::CalcTerritoryChange(const noBaseBuilding& building, const unsigned previewRadius)
{
    isTerritoryGain = previewRadius > 0;
    const unsigned radius = (isTerritoryGain ? previewRadius : building.GetMilitaryRadius()) + TERRITORY_ADD_RADIUS;
    const TerritoryRegion region = world.CreateTerritoryRegion(
      building, radius, isTerritoryGain ? TerritoryChangeReason::Build : TerritoryChangeReason::Destroyed,
      previewRadius);

    const uint8_t owner = building.GetPlayer() + 1;
    RTTR_FOREACH_PT(Position, region.size)
    {
        const MapPoint curMapPt = world.MakeMapPoint(pt + region.startPt);
        const bool isOwned = world.GetNode(curMapPt).owner == owner;
        const bool willBeOwned = region.GetOwner(pt) == owner;
        if(isTerritoryGain ? (willBeOwned && !isOwned) : (isOwned && !willBeOwned))
            territoryChange.push_back(curMapPt);
    }
    std::sort(territoryChange.begin(), territoryChange.end(), MapPointLess{});
}

std::optional<BuildingRangeKind> BuildingRanges::GetKind(const MapPoint pt) const
{
    if(std::binary_search(territoryChange.begin(), territoryChange.end(), pt, MapPointLess{}))
        return BuildingRangeKind::TerritoryChange;
    for(const Range& range : ranges)
    {
        if(world.CalcDistance(pt, range.center) <= range.radius)
            return range.kind;
    }
    return std::nullopt;
}
