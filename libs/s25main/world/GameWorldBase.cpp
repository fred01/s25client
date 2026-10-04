// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "world/GameWorldBase.h"
#include "BQCalculator.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "MapGeometry.h"
#include "RttrForeachPt.h"
#include "SoundManager.h"
#include "TradePathCache.h"
#include "addons/const_addons.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "figures/nofPassiveSoldier.h"
#include "helpers/EnumRange.h"
#include "helpers/IdRange.h"
#include "helpers/containerUtils.h"
#include "lua/LuaInterfaceGame.h"
#include "notifications/NodeNote.h"
#include "notifications/PlayerNodeNote.h"
#include "pathfinding/FreePathFinder.h"
#include "pathfinding/RoadPathFinder.h"
#include "pathfinding/ShipPathData.h"
#include "world/TerritoryRegion.h"
#include "nodeObjs/noFlag.h"
#include "gameData/BuildingProperties.h"
#include "gameData/GameConsts.h"
#include "gameData/TerrainDesc.h"
#include <utility>

GameWorldBase::GameWorldBase(std::vector<GamePlayer> players, const GlobalGameSettings& gameSettings, EventManager& em)
    : roadPathFinder(new RoadPathFinder(*this)), freePathFinder(new FreePathFinder(*this)), players(std::move(players)),
      gameSettings(gameSettings), em(em), soundManager(std::make_unique<SoundManager>()), lua(nullptr), gi(nullptr)
{}

GameWorldBase::~GameWorldBase() = default;

void GameWorldBase::Init(const MapExtent& mapSize, DescIdx<LandscapeDesc> lt)
{
    RTTR_Assert(GetDescription().terrain.size() > 0); // Must have game data initialized
    World::Init(mapSize, lt);
    freePathFinder->Init(mapSize);
}

void GameWorldBase::InitAfterLoad()
{
    RTTR_FOREACH_PT(MapPoint, GetSize())
        RecalcBQ(pt);
}

GamePlayer& GameWorldBase::GetPlayer(const unsigned id)
{
    RTTR_Assert(id < GetNumPlayers());
    return players[id];
}

const GamePlayer& GameWorldBase::GetPlayer(const unsigned id) const
{
    RTTR_Assert(id < GetNumPlayers());
    return players[id];
}

unsigned GameWorldBase::GetNumPlayers() const
{
    return players.size();
}

s25util::span<GamePlayer> GameWorldBase::getPlayers()
{
    return players;
}

s25util::span<const GamePlayer> GameWorldBase::getPlayers() const
{
    return players;
}

bool GameWorldBase::IsSinglePlayer() const
{
    bool foundPlayer = false;
    for(const PlayerInfo& player : players)
    {
        if(player.ps == PlayerState::Occupied)
        {
            if(foundPlayer)
                return false;
            else
                foundPlayer = true;
        }
    }
    return true;
}

ShipPathData& GameWorldBase::GetShipPathData() const
{
    if(!shipPathData)
        shipPathData = std::make_unique<ShipPathData>(*this);
    return *shipPathData;
}

bool GameWorldBase::IsRoadAvailable(const bool boat_road, const MapPoint pt) const
{
    // Hindernisse
    if(GetNode(pt).obj)
    {
        BlockingManner bm = GetNode(pt).obj->GetBM();
        if(bm != BlockingManner::None)
            return false;
    }

    // dont build on the border
    if(GetNode(pt).boundary_stones[BorderStonePos::OnPoint])
        return false;

    for(const auto dir : helpers::EnumRange<Direction>{})
    {
        // Roads around charburner piles are not possible
        if(GetNO(GetNeighbour(pt, dir))->GetBM() == BlockingManner::NothingAround)
            return false;

        // Other roads at this point?
        if(GetPointRoad(pt, dir) != PointRoad::None)
            return false;
    }

    // Terrain (unterscheiden, ob Wasser und Landweg)
    if(!boat_road)
    {
        bool flagPossible = false;

        for(const DescIdx<TerrainDesc> tIdx : GetTerrainsAround(pt))
        {
            const TerrainBQ bq = GetDescription().get(tIdx).GetBQ();
            if(bq == TerrainBQ::Danger)
                return false;
            else if(bq != TerrainBQ::Nothing)
                flagPossible = true;
        }

        return flagPossible;
    } else
    {
        // Beim Wasserweg muss um den Punkt herum Wasser sein
        if(!IsWaterPoint(pt))
            return false;
    }

    return true;
}

bool GameWorldBase::RoadAlreadyBuilt(const bool /*boat_road*/, const MapPoint start,
                                     const std::vector<Direction>& route)
{
    MapPoint tmp(start);
    for(unsigned i = 0; i < route.size() - 1; ++i)
    {
        // Richtiger Weg auf diesem Punkt?
        if(GetPointRoad(tmp, route[i]) == PointRoad::None)
            return false;

        tmp = GetNeighbour(tmp, route[i]);
    }
    return true;
}

bool GameWorldBase::IsOnRoad(const MapPoint& pt) const
{
    // This must be fast for BQ calculation so don't use GetVisiblePointRoad
    for(const auto roadDir : helpers::EnumRange<RoadDir>{})
        if(GetRoad(pt, roadDir) != PointRoad::None)
            return true;
    for(const auto roadDir : helpers::EnumRange<RoadDir>{})
    {
        const Direction oppositeDir = getOppositeDir(roadDir);
        if(GetRoad(GetNeighbour(pt, oppositeDir), roadDir) != PointRoad::None)
            return true;
    }
    return false;
}

bool GameWorldBase::IsFlagAround(const MapPoint& pt) const
{
    for(const MapPoint nb : GetNeighbours(pt))
    {
        if(GetNO(nb)->GetBM() == BlockingManner::Flag)
            return true;
    }
    return false;
}

bool GameWorldBase::IsAnyNeighborOwned(const MapPoint& pt) const
{
    for(const MapPoint& nb : GetNeighbours(pt))
        if(GetNode(nb).owner)
            return true;
    return false;
}

void GameWorldBase::RecalcBQForRoad(const MapPoint pt)
{
    RecalcBQ(pt);

    for(const Direction dir : {Direction::East, Direction::SouthEast, Direction::SouthWest})
        RecalcBQ(GetNeighbour(pt, dir));
}

namespace {
bool IsMilBldOfOwner(const GameWorldBase& gwb, MapPoint pt, unsigned char owner)
{
    return gwb.IsMilitaryBuildingOnNode(pt, false) && (gwb.GetNode(pt).owner == owner);
}
} // namespace

bool GameWorldBase::IsMilitaryBuildingNearNode(const MapPoint nPt, const unsigned char player) const
{
    // Im Umkreis von 4 Punkten ein Militärgebäude suchen
    return CheckPointsInRadius(
      nPt, 4, [this, player](auto pt, auto) { return IsMilBldOfOwner(*this, pt, player + 1); }, false);
}

bool GameWorldBase::IsMilitaryBuildingOnNode(const MapPoint pt, bool attackBldsOnly) const
{
    const noBase* obj = GetNO(pt);
    if(obj->GetType() == NodalObjectType::Building || obj->GetType() == NodalObjectType::Buildingsite)
    {
        BuildingType buildingType = static_cast<const noBaseBuilding*>(obj)->GetBuildingType();
        if(BuildingProperties::IsMilitary(buildingType))
            return true;
        if(!attackBldsOnly
           && (buildingType == BuildingType::Headquarters || buildingType == BuildingType::HarborBuilding))
            return true;
    }

    return false;
}

sortedMilitaryBlds GameWorldBase::LookForMilitaryBuildings(const MapPoint pt, unsigned short radius) const
{
    return militarySquares.GetBuildingsInRange(pt, radius);
}

TerritoryRegion GameWorldBase::CreateTerritoryRegion(const noBaseBuilding& building, unsigned radius,
                                                     TerritoryChangeReason reason, unsigned previewRadius) const
{
    const MapPoint bldPos = building.GetPos();

    // Span at most half the map size (assert even sizes, given due to layout)
    RTTR_Assert(GetWidth() % 2 == 0);
    RTTR_Assert(GetHeight() % 2 == 0);
    Extent halfSize(GetSize() / 2u);
    Extent radius2D = elMin(Extent::all(radius), halfSize);

    // Koordinaten erzeugen für TerritoryRegion
    const Position startPt = Position(bldPos) - radius2D;
    // If we want to check the same number of points right of bld as left we need a +1.
    // But we can't check more than the whole map.
    const Extent size = elMin(2u * radius2D + Extent(1, 1), Extent(GetSize()));
    TerritoryRegion region(startPt, size, *this);

    // Alle Gebäude ihr Terrain in der Nähe neu berechnen
    sortedMilitaryBlds buildings = LookForMilitaryBuildings(bldPos, 3);
    for(const nobBaseMilitary* milBld : buildings)
    {
        if(reason != TerritoryChangeReason::Destroyed || milBld != &building)
            region.CalcTerritoryOfBuilding(*milBld);
    }

    // Baustellen von Häfen mit einschließen
    for(const noBuildingSite* bldSite : harbor_building_sites_from_sea)
    {
        if(reason != TerritoryChangeReason::Destroyed || bldSite != &building)
            region.CalcTerritoryOfBuilding(*bldSite);
    }
    if(previewRadius > 0)
        region.CalcTerritoryOfBuilding(bldPos, building.GetPlayer(), previewRadius);
    CleanTerritoryRegion(region, reason, building);

    return region;
}

void GameWorldBase::CleanTerritoryRegion(TerritoryRegion& region, TerritoryChangeReason reason,
                                         const noBaseBuilding& triggerBld) const
{
    if(GetGGS().isEnabled(AddonId::NO_ALLIED_PUSH))
    {
        const bool isHq = triggerBld.GetBuildingType() == BuildingType::Headquarters;
        const auto newOwnerOfTriggerBld = region.GetOwner(region.GetPosFromMapPos(triggerBld.GetPos()));
        // An HQ can be placed independently of the current owner.
        // So ensure the HQ position is always considered to belong to the HQ owner
        // if the TerritoryChangeReason is Build
        const auto ownerOfTriggerBld =
          isHq && reason == TerritoryChangeReason::Build ? newOwnerOfTriggerBld : GetNode(triggerBld.GetPos()).owner;

        RTTR_FOREACH_PT(Position, region.size)
        {
            const MapPoint curMapPt = MakeMapPoint(pt + region.startPt);
            const auto oldOwner = GetNode(curMapPt).owner;
            const auto newOwner = region.GetOwner(pt);

            // If nothing changed, there is nothing to do (ownerChanged was already initialized)
            if(oldOwner == newOwner)
                continue;

            const bool ownersAllied = oldOwner > 0 && newOwner > 0 && GetPlayer(oldOwner - 1).IsAlly(newOwner - 1);
            if(
              // rule 1: only take territory from an ally if that ally loses a building
              // special case: headquarter can take territory
              (ownersAllied && (ownerOfTriggerBld != oldOwner || reason == TerritoryChangeReason::Build) && !isHq) ||
              // rule 2: do not gain territory when you lose a building (captured or destroyed)
              (ownerOfTriggerBld == newOwner && reason != TerritoryChangeReason::Build) ||
              // rule 3: do not lose territory when you gain a building (newBuilt or capture)
              ((ownerOfTriggerBld == oldOwner && oldOwner > 0 && reason == TerritoryChangeReason::Build)
               || (newOwnerOfTriggerBld == oldOwner && reason == TerritoryChangeReason::Captured)))
            {
                region.SetOwner(pt, oldOwner);
            }
        }
    }

    // "Cosmetics": Remove points that do not border to territory to avoid edges of border stones
    RTTR_FOREACH_PT(Position, region.size)
    {
        uint8_t owner = region.GetOwner(pt);
        if(!owner)
            continue;
        // Check if any neighbour is fully surrounded by player territory
        bool isPlayerTerritoryNear = false;
        for(const auto d : helpers::EnumRange<Direction>{})
        {
            Position neighbour = ::GetNeighbour(pt + region.startPt, d);
            if(region.SafeGetOwner(neighbour - region.startPt) != owner)
                continue;
            // Don't check this point as it is always true
            const Direction exceptDir = d + 3u;
            if(region.WillBePlayerTerritory(neighbour, owner, exceptDir))
            {
                isPlayerTerritoryNear = true;
                break;
            }
        }

        // All good?
        if(isPlayerTerritoryNear)
            continue;
        // No neighbouring player territory found. Look for another
        uint8_t newOwner = 0;
        for(const auto d : helpers::EnumRange<Direction>{})
        {
            Position neighbour = ::GetNeighbour(pt + region.startPt, d);
            uint8_t nbOwner = region.SafeGetOwner(neighbour - region.startPt);
            // No or same player?
            if(!nbOwner || nbOwner == owner)
                continue;
            // Don't check this point as it would always fail
            const Direction exceptDir = d + 3u;
            // First one found gets it
            if(region.WillBePlayerTerritory(neighbour, nbOwner, exceptDir))
            {
                newOwner = nbOwner;
                break;
            }
        }
        region.SetOwner(pt, newOwner);
    }
}

noFlag* GameWorldBase::GetRoadFlag(MapPoint pt, Direction& dir, const helpers::OptionalEnum<Direction> prevDir)
{
    // Getting a flag is const
    const noFlag* flag = const_cast<const GameWorldBase*>(this)->GetRoadFlag(pt, dir, prevDir);
    // However we self are not const, so we allow returning a non-const flag pointer
    return const_cast<noFlag*>(flag);
}

const noFlag* GameWorldBase::GetRoadFlag(MapPoint pt, Direction& dir, helpers::OptionalEnum<Direction> prevDir) const
{
    while(true)
    {
        // suchen, wo der Weg weitergeht
        helpers::OptionalEnum<Direction> nextDir;
        for(const auto i : helpers::EnumRange<Direction>{})
        {
            if(i != prevDir && GetPointRoad(pt, i) != PointRoad::None)
            {
                nextDir = i;
                break;
            }
        }

        if(!nextDir)
            return nullptr;

        pt = GetNeighbour(pt, *nextDir);

        // endlich am Ende des Weges und an einer Flagge angekommen?
        if(GetNO(pt)->GetType() == NodalObjectType::Flag)
        {
            dir = *nextDir + 3u;
            return GetSpecObj<noFlag>(pt);
        }
        prevDir = *nextDir + 3u;
    }
}

Position GameWorldBase::GetNodePos(const MapPoint pt) const
{
    return ::GetNodePos(pt, GetNode(pt).altitude);
}

void GameWorldBase::VisibilityChanged(const MapPoint pt, unsigned player, Visibility /*oldVis*/, Visibility /*newVis*/)
{
    GetNotifications().publish(PlayerNodeNote(PlayerNodeNote::Visibility, pt, player));
}

/// Verändert die Höhe eines Punktes und die damit verbundenen Schatten
void GameWorldBase::AltitudeChanged(const MapPoint pt)
{
    RecalcBQAroundPointBig(pt);
    GetNotifications().publish(NodeNote(NodeNote::Altitude, pt));
}

void GameWorldBase::RecalcBQAroundPoint(const MapPoint pt)
{
    RecalcBQ(pt);
    for(const MapPoint nb : GetNeighbours(pt))
        RecalcBQ(nb);
}

void GameWorldBase::RecalcBQAroundPointBig(const MapPoint pt)
{
    // Point and radius 1
    RecalcBQAroundPoint(pt);
    // And radius 2
    for(unsigned i = 0; i < 12; ++i)
        RecalcBQ(GetNeighbour2(pt, i));
}

Visibility GameWorldBase::CalcVisiblityWithAllies(const MapPoint pt, const unsigned char player) const
{
    const MapNode& node = GetNode(pt);
    Visibility best_visibility = node.fow[player].visibility;

    if(best_visibility == Visibility::Visible)
        return best_visibility;

    /// Teamsicht aktiviert?
    if(GetGGS().teamView)
    {
        const GamePlayer& curPlayer = GetPlayer(player);
        // Dann prüfen, ob Teammitglieder evtl. eine bessere Sicht auf diesen Punkt haben
        for(unsigned i = 0; i < GetNumPlayers(); ++i)
        {
            if(i != player && curPlayer.IsAlly(i))
            {
                if(node.fow[i].visibility > best_visibility)
                    best_visibility = node.fow[i].visibility;
            }
        }
    }

    return best_visibility;
}

bool GameWorldBase::IsCoastalPointToSeaWithHarbor(const MapPoint pt) const
{
    SeaId sea = GetSeaFromCoastalPoint(pt);
    if(sea)
    {
        for(const auto i : helpers::idRange<HarborId>(GetNumHarborPoints()))
        {
            if(IsHarborAtSea(HarborId(i), sea))
                return true;
        }
    }
    return false;
}

template<typename T_IsHarborOk>
HarborId GameWorldBase::GetHarborInDir(const MapPoint pt, const HarborId originHarborId, const ShipDirection& dir,
                                       T_IsHarborOk isHarborOk) const
{
    RTTR_Assert(originHarborId);

    const MapPoint hbPt = GetHarborPoint(originHarborId);

    // Find sea of harbor point
    SeaId seaId;
    for(const auto d : helpers::EnumRange<Direction>{})
    {
        if(GetNeighbour(hbPt, d) == pt)
        {
            seaId = GetSeaId(originHarborId, d);
            break;
        }
    }
    RTTR_Assert(seaId);

    const std::vector<HarborPos::Neighbor>& neighbors = GetHarborNeighbors(originHarborId, dir);

    for(const auto& neighbor : neighbors)
    {
        if(IsHarborAtSea(neighbor.id, seaId) && isHarborOk(neighbor.id))
            return neighbor.id;
    }

    // Nichts gefunden
    return HarborId::invalidValue();
}

/// Functor that returns true, when the owner of a point is set and different than the player
struct IsPointOwnerDifferent
{
    const GameWorldBase& gwb;
    // Owner to compare. Note that owner=0 --> No owner => owner=player+1
    const unsigned char cmpOwner;

    IsPointOwnerDifferent(const GameWorldBase& gwb, const unsigned char player) : gwb(gwb), cmpOwner(player + 1) {}

    bool operator()(const MapPoint pt, unsigned /*distance*/) const
    {
        const unsigned char owner = gwb.GetNode(pt).owner;
        return owner != 0 && owner != cmpOwner;
    }
};

/// Ist es an dieser Stelle für einen Spieler möglich einen Hafen zu bauen
bool GameWorldBase::IsHarborPointFree(const HarborId harborId, const unsigned char player) const
{
    MapPoint hbPos(GetHarborPoint(harborId));

    // Überprüfen, ob das Gebiet in einem bestimmten Radius entweder vom Spieler oder gar nicht besetzt ist außer wenn
    // der Hafen und die Flagge im Spielergebiet liegen
    MapPoint flagPos = GetNeighbour(hbPos, Direction::SouthEast);
    if(GetNode(hbPos).owner != player + 1 || GetNode(flagPos).owner != player + 1)
    {
        if(CheckPointsInRadius(hbPos, 4, IsPointOwnerDifferent(*this, player), false))
            return false;
    }

    return GetNode(hbPos).bq == BuildingQuality::Harbor;
}

/// Sucht freie Hafenpunkte, also wo noch ein Hafen gebaut werden kann
HarborId GameWorldBase::GetNextFreeHarborPoint(const MapPoint pt, const HarborId originHarborId,
                                               const ShipDirection& dir, const unsigned char player) const
{
    return GetHarborInDir(pt, originHarborId, dir,
                          [this, player](auto harborId) { return this->IsHarborPointFree(harborId, player); });
}

/// Bestimmt für einen beliebigen Punkt auf der Karte die Entfernung zum nächsten Hafenpunkt
unsigned GameWorldBase::CalcDistanceToNearestHarbor(const MapPoint pos) const
{
    unsigned min_distance = std::numeric_limits<unsigned>::max();
    for(const auto i : helpers::idRange<HarborId>(GetNumHarborPoints()))
        min_distance = std::min(min_distance, this->CalcDistance(pos, GetHarborPoint(i)));

    return min_distance;
}

/// returns true when a harborpoint is in SEAATTACK_DISTANCE for figures!
bool GameWorldBase::IsAHarborInSeaAttackDistance(const MapPoint pos) const
{
    for(const auto i : helpers::idRange<HarborId>(GetNumHarborPoints()))
    {
        if(CalcDistance(pos, GetHarborPoint(i)) < SEAATTACK_DISTANCE)
        {
            if(FindHumanPath(pos, GetHarborPoint(i), SEAATTACK_DISTANCE))
                return true;
        }
    }
    return false;
}

std::vector<HarborId> GameWorldBase::GetUsableTargetHarborsForAttack(const MapPoint targetPt,
                                                                     std::vector<bool>& use_seas,
                                                                     const unsigned char player_attacker) const
{
    // Walk to the flag of the bld/harbor. Important to check because in some locations where the coast is north of the
    // harbor this might be blocked
    const MapPoint flagPt = GetNeighbour(targetPt, Direction::SouthEast);
    std::vector<HarborId> harbor_points;
    // Check each possible harbor
    for(const auto curHbId : helpers::idRange<HarborId>(GetNumHarborPoints()))
    {
        const MapPoint harborPt = GetHarborPoint(curHbId);

        if(CalcDistance(harborPt, targetPt) > SEAATTACK_DISTANCE)
            continue;

        // Not attacking this harbor and harbors block?
        if(targetPt != harborPt && GetGGS().getSelection(AddonId::SEA_ATTACK) == 1)
        {
            // Does an enemy harbor exist at current harbor spot? -> Can't attack through this harbor spot
            const auto* hb = GetSpecObj<nobHarborBuilding>(harborPt);
            if(hb && GetPlayer(player_attacker).IsAttackable(hb->GetPlayer()))
                continue;
        }

        // add seaIds from which we can actually attack the harbor
        bool harborinlist = false;
        for(const auto dir : helpers::enumRange<Direction>())
        {
            const SeaId seaId = GetSeaId(curHbId, dir);
            if(!seaId)
                continue;
            // checks previously tested sea ids to skip pathfinding
            bool previouslytested = false;
            for(unsigned k = 0; k < rttr::enum_cast(dir); k++)
            {
                if(seaId == GetSeaId(curHbId, Direction(k)))
                {
                    previouslytested = true;
                    break;
                }
            }
            if(previouslytested)
                continue;

            // Can figures reach flag from coast
            const MapPoint coastalPt = GetCoastalPoint(curHbId, seaId);
            if((flagPt == coastalPt) || FindHumanPath(flagPt, coastalPt, SEAATTACK_DISTANCE))
            {
                use_seas.at(seaId.value() - 1) = true;
                if(!harborinlist)
                {
                    harbor_points.push_back(curHbId);
                    harborinlist = true;
                }
            }
        }
    }
    return harbor_points;
}

std::vector<SeaId> GameWorldBase::GetFilteredSeaIDsForAttack(const MapPoint targetPt,
                                                             const std::vector<SeaId>& usableSeas,
                                                             const unsigned char player_attacker) const
{
    // Walk to the flag of the bld/harbor. Important to check because in some locations where the coast is north of the
    // harbor this might be blocked
    const MapPoint flagPt = GetNeighbour(targetPt, Direction::SouthEast);
    std::vector<SeaId> confirmedSeaIds;
    // Check each possible harbor
    for(const auto curHbId : helpers::idRange<HarborId>(GetNumHarborPoints()))
    {
        const MapPoint harborPt = GetHarborPoint(curHbId);

        if(CalcDistance(harborPt, targetPt) > SEAATTACK_DISTANCE)
            continue;

        // Not attacking this harbor and harbors block?
        if(targetPt != harborPt && GetGGS().getSelection(AddonId::SEA_ATTACK) == 1)
        {
            // Does an enemy harbor exist at current harbor spot? -> Can't attack through this harbor spot
            const auto* hb = GetSpecObj<nobHarborBuilding>(harborPt);
            if(hb && GetPlayer(player_attacker).IsAttackable(hb->GetPlayer()))
                continue;
        }

        for(const auto dir : helpers::enumRange<Direction>())
        {
            const SeaId seaId = GetSeaId(curHbId, dir);
            if(!seaId)
                continue;
            // sea id is not in compare list or already confirmed? -> skip rest
            if(!helpers::contains(usableSeas, seaId) || helpers::contains(confirmedSeaIds, seaId))
                continue;

            // checks previously tested sea ids to skip pathfinding
            bool previouslytested = false;
            for(unsigned k = 0; k < rttr::enum_cast(dir); k++)
            {
                if(seaId == GetSeaId(curHbId, Direction(k)))
                {
                    previouslytested = true;
                    break;
                }
            }
            if(previouslytested)
                continue;

            // Can figures reach flag from coast
            MapPoint coastalPt = GetCoastalPoint(curHbId, seaId);
            if((flagPt == coastalPt) || FindHumanPath(flagPt, coastalPt, SEAATTACK_DISTANCE))
            {
                confirmedSeaIds.push_back(seaId);
                // all sea ids confirmed? return without changes
                if(confirmedSeaIds.size() == usableSeas.size())
                    return confirmedSeaIds;
            }
        }
    }
    return confirmedSeaIds;
}

/// Liefert Hafenpunkte im Umkreis von einem bestimmten Militärgebäude
std::vector<HarborId> GameWorldBase::GetHarborPointsAroundMilitaryBuilding(const MapPoint pt) const
{
    std::vector<HarborId> harbor_points;
    // Nach Hafenpunkten in der Nähe des angegriffenen Gebäudes suchen
    // Alle unsere Häfen durchgehen
    for(const auto i : helpers::idRange<HarborId>(GetNumHarborPoints()))
    {
        const MapPoint harborPt = GetHarborPoint(i);

        if(CalcDistance(harborPt, pt) <= SEAATTACK_DISTANCE)
        {
            // Wird ein Weg vom Militärgebäude zum Hafen gefunden bzw. Ziel = Hafen?
            if(pt == harborPt || FindHumanPath(pt, harborPt, SEAATTACK_DISTANCE))
                harbor_points.push_back(i);
        }
    }
    return harbor_points;
}

/// Gibt Anzahl oder geschätzte Stärke(rang summe + anzahl) der verfügbaren Soldaten die zu einem Schiffsangriff starten
/// können von einer bestimmten sea id aus
unsigned GameWorldBase::GetNumSoldiersForSeaAttackAtSea(const unsigned char player_attacker, SeaId sea,
                                                        bool returnCount) const
{
    // Liste alle Militärgebäude des Angreifers, die Soldaten liefern
    std::vector<nobHarborBuilding::SeaAttackerBuilding> buildings;
    unsigned attackercount = 0;
    // Angrenzende Häfen des Angreifers an den entsprechenden Meeren herausfinden
    const std::list<nobHarborBuilding*>& harbors = GetPlayer(player_attacker).GetBuildingRegister().GetHarbors();
    for(auto* harbor : harbors)
    {
        // Bestimmen, ob Hafen an einem der Meere liegt, über die sich auch die gegnerischen
        // Hafenpunkte erreichen lassen
        if(!IsHarborAtSea(harbor->GetHarborPosID(), sea))
            continue;

        std::vector<nobHarborBuilding::SeaAttackerBuilding> tmp = harbor->GetAttackerBuildingsForSeaIdAttack();
        buildings.insert(buildings.begin(), tmp.begin(), tmp.end());
    }

    // Die Soldaten aus allen Militärgebäuden sammeln
    for(auto& building : buildings)
    {
        // Soldaten holen
        std::vector<nofPassiveSoldier*> tmp_soldiers =
          building.building->GetSoldiersForAttack(building.harbor->GetPos());

        // Überhaupt welche gefunden?
        if(tmp_soldiers.empty())
            continue;

        // Soldaten hinzufügen
        for(auto& tmp_soldier : tmp_soldiers)
        {
            if(returnCount)
                attackercount++;
            else
                attackercount += (tmp_soldier->GetRank() + 1); // private is rank 0 -> increase by 1-5
        }
    }
    return attackercount;
}

/// Sucht verfügbare Soldaten, um dieses Militärgebäude mit einem Seeangriff anzugreifen
std::vector<GameWorldBase::PotentialSeaAttacker>
GameWorldBase::GetSoldiersForSeaAttack(const unsigned char player_attacker, const MapPoint pt) const
{
    std::vector<GameWorldBase::PotentialSeaAttacker> attackers;
    // sea attack abgeschaltet per addon?
    if(!GetGGS().isEnabled(AddonId::SEA_ATTACK))
        return attackers;
    // Do we have an attackble military building?
    const auto* milBld = GetSpecObj<nobBaseMilitary>(pt);
    if(!milBld || !milBld->IsAttackable(player_attacker))
        return attackers;
    std::vector<bool> use_seas(GetNumSeas());

    // Mögliche Hafenpunkte in der Nähe des Gebäudes
    std::vector<HarborId> defender_harbors = GetUsableTargetHarborsForAttack(pt, use_seas, player_attacker);

    // Liste alle Militärgebäude des Angreifers, die Soldaten liefern
    std::vector<nobHarborBuilding::SeaAttackerBuilding> buildings;

    // Angrenzende Häfen des Angreifers an den entsprechenden Meeren herausfinden
    const std::list<nobHarborBuilding*>& harbors = GetPlayer(player_attacker).GetBuildingRegister().GetHarbors();
    for(auto* harbor : harbors)
    {
        // Bestimmen, ob Hafen an einem der Meere liegt, über die sich auch die gegnerischen
        // Hafenpunkte erreichen lassen
        bool is_at_sea = false;
        for(const auto dir : helpers::EnumRange<Direction>{})
        {
            const SeaId seaId = GetSeaId(harbor->GetHarborPosID(), dir);
            if(seaId && use_seas[seaId.value() - 1])
            {
                is_at_sea = true;
                break;
            }
        }

        if(!is_at_sea)
            continue;

        std::vector<nobHarborBuilding::SeaAttackerBuilding> tmp =
          harbor->GetAttackerBuildingsForSeaAttack(defender_harbors);
        for(auto& itBld : tmp)
        {
            // Check if the building was already inserted
            auto oldBldIt =
              helpers::find_if(buildings, nobHarborBuilding::SeaAttackerBuilding::CmpBuilding(itBld.building));
            if(oldBldIt == buildings.end())
            {
                // Not found -> Add
                buildings.push_back(itBld);
            } else if(oldBldIt->distance > itBld.distance
                      || (oldBldIt->distance == itBld.distance
                          && oldBldIt->harbor->GetObjId() > itBld.harbor->GetObjId()))
            {
                // New distance is smaller (with tie breaker for async prevention) -> update
                *oldBldIt = itBld;
            }
        }
    }

    // Die Soldaten aus allen Militärgebäuden sammeln
    for(const auto& bld : buildings)
    {
        // Soldaten holen
        std::vector<nofPassiveSoldier*> tmp_soldiers = bld.building->GetSoldiersForAttack(bld.harbor->GetPos());

        // Soldaten hinzufügen
        for(nofPassiveSoldier* soldier : tmp_soldiers)
        {
            RTTR_Assert(!helpers::contains_if(attackers, PotentialSeaAttacker::CmpSoldier(soldier)));
            attackers.push_back(PotentialSeaAttacker(soldier, bld.harbor, bld.distance));
        }
    }

    return attackers;
}

void GameWorldBase::RecalcBQ(const MapPoint pt)
{
    BQCalculator calcBQ(*this);
    if(SetBQ(pt, calcBQ(pt, [this](auto pt) { return this->IsOnRoad(pt); })))
    {
        GetNotifications().publish(NodeNote(NodeNote::BQ, pt));
    }
}

void GameWorldBase::SetComputerBarrier(const MapPoint& pt, unsigned radius)
{
    for(const auto& pt : GetPointsInRadiusWithCenter(pt, radius))
        ptsInsideComputerBarriers.insert(pt);
}

bool GameWorldBase::IsInsideComputerBarrier(const MapPoint& pt) const
{
    return helpers::contains(ptsInsideComputerBarriers, pt);
}
