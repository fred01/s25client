// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "RttrForeachPt.h"
#include "SerializedGameData.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobUsual.h"
#include "factories/BuildingFactory.h"
#include "figures/noFigure.h"
#include "pathfinding/PathConditionHuman.h"
#include "worldFixtures/MockLocalGameState.h"
#include "worldFixtures/WorldWithGCExecution.h"
#include "worldFixtures/terrainHelpers.h"
#include "nodeObjs/noFlag.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(Waterways)

namespace {
/// A river at x=10..11 (x=9 and x=12 are the coasts) with a waterway across it.
/// The woodcutter on the other side is only connected to the HQ by this waterway.
struct WaterwayFixture : WorldWithGCExecution1P
{
    nobBaseWarehouse* hq;
    MapPoint flagA, flagB, bldFlag, bldPos;

    WaterwayFixture() : hq(world.GetSpecObj<nobBaseWarehouse>(hqPos))
    {
        const auto tWater = GetWaterTerrain(world.GetDescription());
        RTTR_FOREACH_PT(MapPoint, world.GetSize())
        {
            if(pt.x >= 9 && pt.x <= 11)
            {
                MapNode& node = world.GetNodeWriteable(pt);
                node.t1 = node.t2 = tWater;
            }
        }
        world.InitAfterLoad();

        const MapPoint hqFlag = world.GetNeighbour(hqPos, Direction::SouthEast);
        flagA = MapPoint(9, hqFlag.y);
        flagB = world.MakeMapPoint(Position(12, hqFlag.y));
        bldFlag = world.MakeMapPoint(Position(flagB.x + 2, flagB.y));
        bldPos = world.GetNeighbour(bldFlag, Direction::NorthWest);
        BOOST_TEST_REQUIRE(!PathConditionHuman(world).IsNodeOk(MapPoint(10, hqFlag.y)));
        BOOST_TEST_REQUIRE(!PathConditionHuman(world).IsNodeOk(MapPoint(11, hqFlag.y)));

        this->BuildRoad(hqFlag, false, std::vector<Direction>(flagA.x - hqFlag.x, Direction::East));
        BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(flagA));
        this->BuildRoad(flagA, true, std::vector<Direction>(3, Direction::East));
        BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(flagB));
        BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(flagB)->GetRoute(Direction::West));

        BuildingFactory::CreateBuilding(world, BuildingType::Woodcutter, bldPos, curPlayer, Nation::Romans);
        this->BuildRoad(flagB, false, std::vector<Direction>(2, Direction::East));
        BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(flagB)->GetRoute(Direction::East));

        hq->AddToInventory(PeopleCounts::make(Job::Woodcutter, 1), true);
    }

    nobUsual* woodcutter() { return world.GetSpecObj<nobUsual>(bldPos); }
    bool hasPathToBld()
    {
        return world.FindHumanPathOnRoads(*hq, *world.GetSpecObj<noFlag>(bldFlag)) != RoadPathDirection::None;
    }
    /// Return a figure travelling by boat, if any
    noFigure* figureInBoat()
    {
        for(const MapPoint pt : {flagA, MapPoint(10, flagA.y), MapPoint(11, flagA.y), flagB})
        {
            for(noBase& obj : world.GetFigures(pt))
            {
                if(obj.GetType() == NodalObjectType::Figure && static_cast<noFigure&>(obj).IsInBoat())
                    return &static_cast<noFigure&>(obj);
            }
        }
        return nullptr;
    }
};
} // namespace

BOOST_FIXTURE_TEST_CASE(SettlersDontUseWaterwaysByDefault, WaterwayFixture)
{
    BOOST_TEST(!hasPathToBld());
    RTTR_SKIP_GFS(1000);
    BOOST_TEST(!woodcutter()->HasWorker());
    BOOST_TEST(hq->GetNumRealFigures(Job::Woodcutter) > 0u);
}

BOOST_FIXTURE_TEST_CASE(SettlersUseWaterways, WaterwayFixture)
{
    ggs.setSelection(AddonId::SETTLERS_USE_WATERWAYS, 1);
    BOOST_TEST(hasPathToBld());
    // Request a worker now that the building is reachable
    world.GetPlayer(curPlayer).FindWarehouseForAllJobs();
    RTTR_EXEC_TILL(1000, figureInBoat() != nullptr);
    RTTR_EXEC_TILL(1000, woodcutter()->HasWorker());
    BOOST_TEST(!figureInBoat());
}

BOOST_FIXTURE_TEST_CASE(SettlerPaddlesToShoreWhenWaterwayIsDestroyed, WaterwayFixture)
{
    ggs.setSelection(AddonId::SETTLERS_USE_WATERWAYS, 1);
    const unsigned numWoodcutters = hq->GetNumRealFigures(Job::Woodcutter);
    world.GetPlayer(curPlayer).FindWarehouseForAllJobs();
    // Wait till he is on the water
    const MapPoint waterPt(10, flagA.y);
    RTTR_EXEC_TILL(1000, figureInBoat() && figureInBoat()->GetPos() == waterPt);
    this->DestroyRoad(flagA, Direction::East);
    BOOST_TEST_REQUIRE(!world.GetSpecObj<noFlag>(flagA)->GetRoute(Direction::East));
    // Not on a waterway anymore but still in the boat paddling to the shore
    BOOST_TEST_REQUIRE(figureInBoat());
    BOOST_TEST_REQUIRE(!figureInBoat()->GetCurrentRoad());

    // Save & load while paddling
    SerializedGameData sgd;
    sgd.MakeSnapshot(*game);
    MockLocalGameState lgs;
    em.Clear();
    world.Unload();
    sgd.ReadSnapshot(*game, lgs);
    SerializedGameData sgd2;
    sgd2.MakeSnapshot(*game);
    BOOST_CHECK_EQUAL_COLLECTIONS(sgd.GetData(), sgd.GetData() + sgd.GetLength(), sgd2.GetData(),
                                  sgd2.GetData() + sgd2.GetLength());
    hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(figureInBoat());

    // He reaches the shore and walks back home
    RTTR_EXEC_TILL(3000, hq->GetNumRealFigures(Job::Woodcutter) == numWoodcutters);
}

BOOST_AUTO_TEST_SUITE_END()
