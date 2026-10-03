// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SerializedGameData.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobUsual.h"
#include "factories/BuildingFactory.h"
#include "figures/nofBuildingWorker.h"
#include "postSystem/PostBox.h"
#include "postSystem/PostMsg.h"
#include "worldFixtures/MockLocalGameState.h"
#include "worldFixtures/WorldWithGCExecution.h"
#include "gameData/ToolConsts.h"
#include <rttr/test/LogAccessor.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>

// LCOV_EXCL_START
static std::ostream& operator<<(std::ostream& os, const PostCategory& cat)
{
    return os << static_cast<unsigned>(cat);
}
// LCOV_EXCL_STOP

BOOST_AUTO_TEST_SUITE(Production)

BOOST_FIXTURE_TEST_CASE(MetalWorkerStopped, WorldWithGCExecution1P)
{
    addStartResources();
    rttr::test::LogAccessor logAcc;
    ggs.setSelection(AddonId::TOOL_ORDERING, 1);
    ggs.setSelection(AddonId::METALWORKSBEHAVIORONZERO, 1);
    world.GetSpecObj<nobBaseWarehouse>(hqPos)->AddToInventory(GoodCounts::make(GoodType::Iron, 10), true);
    MapPoint bldPos = hqPos + MapPoint(2, 0);
    BuildingFactory::CreateBuilding(world, BuildingType::Metalworks, bldPos, curPlayer, Nation::Africans);
    this->BuildRoad(world.GetNeighbour(bldPos, Direction::SouthEast), false,
                    std::vector<Direction>(2, Direction::West));
    MapPoint bldPos2 = hqPos - MapPoint(2, 0);
    BuildingFactory::CreateBuilding(world, BuildingType::Metalworks, bldPos2, curPlayer, Nation::Africans);
    this->BuildRoad(world.GetNeighbour(bldPos2, Direction::SouthEast), false,
                    std::vector<Direction>(2, Direction::East));

    helpers::EnumArray<int8_t, Tool> toolOrder;
    ToolSettings toolSettings;
    std::fill(toolOrder.begin(), toolOrder.end(), 0);
    std::fill(toolSettings.begin(), toolSettings.end(), 0);
    this->ChangeTools(toolSettings, toolOrder.data());
    // Get wares and workers in
    RTTR_SKIP_GFS(1000);

    toolOrder[Tool::Tongs] = 1;
    toolOrder[Tool::Cleaver] = 1;
    toolOrder[Tool::Rollingpin] = 1;
    PostBox& postbox = world.GetPostMgr().AddPostBox(0);
    postbox.Clear();
    const Inventory& curInventory = world.GetPlayer(curPlayer).GetInventory();
    Inventory expectedInventory = curInventory;
    expectedInventory.Add(GoodType::Tongs, toolOrder[Tool::Tongs]);
    expectedInventory.Add(GoodType::Cleaver, toolOrder[Tool::Cleaver]);
    expectedInventory.Add(GoodType::Rollingpin, toolOrder[Tool::Rollingpin]);
    // Place order
    this->ChangeTools(toolSettings, toolOrder.data());
    RTTR_REQUIRE_LOG_CONTAINS("Committing an order", true);
    // Wait for completion message
    RTTR_EXEC_TILL(3000, postbox.GetNumMsgs() == 1u);
    BOOST_TEST_REQUIRE(postbox.GetMsg(0)->GetCategory() == PostCategory::Economy);
    // Stop it and wait till goods are produced
    this->SetProductionEnabled(bldPos, false);
    this->SetProductionEnabled(bldPos2, false);
    RTTR_EXEC_TILL(2000, curInventory[GoodType::Tongs] == expectedInventory[GoodType::Tongs]
                           && curInventory[GoodType::Cleaver] == expectedInventory[GoodType::Cleaver]
                           && curInventory[GoodType::Rollingpin] == expectedInventory[GoodType::Rollingpin]);
}

BOOST_FIXTURE_TEST_CASE(MetalWorkerOrders, WorldWithGCExecution1P)
{
    GoodsAndPeopleCounts inv;
    inv[GoodType::Boards] = 20;
    inv[GoodType::Stones] = 20;
    inv[GoodType::Iron] = 10;
    inv[Job::Metalworker] = 1;
    world.GetSpecObj<nobBaseWarehouse>(hqPos)->AddToInventory(inv, true);
    ggs.setSelection(AddonId::METALWORKSBEHAVIORONZERO, 1);
    ggs.setSelection(AddonId::TOOL_ORDERING, 1);
    ToolSettings settings;
    std::fill(settings.begin(), settings.end(), 0);
    this->ChangeTools(settings);
    MapPoint housePos(hqPos.x + 3, hqPos.y);
    const nobUsual* mw = static_cast<nobUsual*>(
      BuildingFactory::CreateBuilding(world, BuildingType::Metalworks, housePos, curPlayer, Nation::Romans));
    MapPoint flagPos = world.GetNeighbour(hqPos, Direction::SouthEast);
    this->BuildRoad(flagPos, false, std::vector<Direction>(3, Direction::East));
    RTTR_EXEC_TILL(200, mw->HasWorker());
    BOOST_TEST_REQUIRE(!mw->is_working);
    // Wait till he has all the wares
    RTTR_EXEC_TILL(3000, mw->GetNumWares(0) == 6);
    RTTR_EXEC_TILL(3000, mw->GetNumWares(1) == 6);
    // No order -> not working
    BOOST_TEST_REQUIRE(!mw->is_working);
    helpers::EnumArray<int8_t, Tool> orders;
    std::fill(orders.begin(), orders.end(), 0);
    orders[Tool::Bow] = 1;
    this->ChangeTools(settings, orders.data());
    RTTR_EXEC_TILL(1300, mw->is_working);
}

namespace {
struct MintFixture : WorldWithGCExecution1P
{
    nobBaseWarehouse* hq;
    nobUsual* mint;
    MapPoint mintPos;
    static constexpr unsigned numGold = 10, numCoal = 10;

    MintFixture() : hq(world.GetSpecObj<nobBaseWarehouse>(hqPos)), mintPos(hqPos.x + 3, hqPos.y)
    {
        GoodsAndPeopleCounts inv;
        inv[GoodType::Gold] = numGold;
        inv[GoodType::Coal] = numCoal;
        inv[Job::Minter] = 1;
        hq->AddToInventory(inv, true);
        mint = static_cast<nobUsual*>(
          BuildingFactory::CreateBuilding(world, BuildingType::Mint, mintPos, curPlayer, Nation::Romans));
        this->BuildRoad(world.GetNeighbour(hqPos, Direction::SouthEast), false,
                        std::vector<Direction>(3, Direction::East));
    }
    unsigned numStoredWares() const { return mint->GetNumWares(0) + mint->GetNumWares(1); }
    /// All gold and coal is either still there or was converted to coins
    bool allWaresInHQ() const
    {
        const unsigned coins = hq->GetNumRealWares(GoodType::Coins);
        return hq->GetNumRealWares(GoodType::Gold) + coins == numGold
               && hq->GetNumRealWares(GoodType::Coal) + coins == numCoal;
    }
};
} // namespace

BOOST_FIXTURE_TEST_CASE(StoppedBuildingKeepsWaresWithoutAddon, MintFixture)
{
    RTTR_EXEC_TILL(3000, numStoredWares() >= 4u);
    this->SetProductionEnabled(mintPos, false);
    // Let the minter finish his current coin
    RTTR_SKIP_GFS(500);
    const unsigned numWares = numStoredWares();
    BOOST_TEST_REQUIRE(numWares > 0u);
    RTTR_SKIP_GFS(2000);
    BOOST_TEST(numStoredWares() == numWares);
}

BOOST_FIXTURE_TEST_CASE(StoppedBuildingCarriesOutWares, MintFixture)
{
    ggs.setSelection(AddonId::CARRY_OUT_WARES_ON_STOP, 1);
    const Inventory& playerInventory = world.GetPlayer(curPlayer).GetInventory();
    RTTR_EXEC_TILL(3000, numStoredWares() >= 4u);
    this->SetProductionEnabled(mintPos, false);
    // Everything gets carried back to the HQ and nothing is created or lost
    RTTR_EXEC_TILL(4000, numStoredWares() == 0u && allWaresInHQ());
    BOOST_TEST(playerInventory[GoodType::Gold] == hq->GetNumRealWares(GoodType::Gold));
    BOOST_TEST(playerInventory[GoodType::Coal] == hq->GetNumRealWares(GoodType::Coal));
    BOOST_TEST(!mint->AreThereAnyOrderedWares());
    // Nothing is ordered while stopped
    RTTR_SKIP_GFS(2000);
    BOOST_TEST(numStoredWares() == 0u);
    BOOST_TEST(allWaresInHQ());
    // Enabling production gets the wares back in
    this->SetProductionEnabled(mintPos, true);
    RTTR_EXEC_TILL(3000, numStoredWares() > 0u);
}

BOOST_FIXTURE_TEST_CASE(StoppingCancelsOrderedWares, MintFixture)
{
    ggs.setSelection(AddonId::CARRY_OUT_WARES_ON_STOP, 1);
    RTTR_EXEC_TILL(500, mint->AreThereAnyOrderedWares());
    this->SetProductionEnabled(mintPos, false);
    RTTR_EXEC_TILL(4000, numStoredWares() == 0u && allWaresInHQ());
    BOOST_TEST(hq->GetNumRealWares(GoodType::Coins) == 0u);
}

BOOST_FIXTURE_TEST_CASE(CarryOutWaresSaveLoad, MintFixture)
{
    ggs.setSelection(AddonId::CARRY_OUT_WARES_ON_STOP, 1);
    RTTR_EXEC_TILL(3000, numStoredWares() >= 4u);
    const unsigned numWaresBeforeStop = numStoredWares();
    this->SetProductionEnabled(mintPos, false);
    // Save while a stored ware is carried out
    RTTR_EXEC_TILL(2000, numStoredWares() < numWaresBeforeStop
                           && mint->GetWorker()->GetState() == nofBuildingWorker::State::CarryoutWare);
    SerializedGameData sgd;
    sgd.MakeSnapshot(*game);
    MockLocalGameState lgs;
    em.Clear();
    world.Unload();
    sgd.ReadSnapshot(*game, lgs);
    // Serialize again and compare data
    SerializedGameData sgd2;
    sgd2.MakeSnapshot(*game);
    BOOST_CHECK_EQUAL_COLLECTIONS(sgd.GetData(), sgd.GetData() + sgd.GetLength(), sgd2.GetData(),
                                  sgd2.GetData() + sgd2.GetLength());

    hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    mint = world.GetSpecObj<nobUsual>(mintPos);
    BOOST_TEST_REQUIRE((mint->GetWorker()->GetState() == nofBuildingWorker::State::CarryoutWare));
    RTTR_EXEC_TILL(4000, numStoredWares() == 0u && allWaresInHQ());
    const Inventory& playerInventory = world.GetPlayer(curPlayer).GetInventory();
    BOOST_TEST(playerInventory[GoodType::Gold] == hq->GetNumRealWares(GoodType::Gold));
    BOOST_TEST(playerInventory[GoodType::Coal] == hq->GetNumRealWares(GoodType::Coal));
}

BOOST_AUTO_TEST_SUITE_END()
