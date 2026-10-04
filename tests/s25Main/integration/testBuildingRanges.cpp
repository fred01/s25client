// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GlobalGameSettings.h"
#include "addons/const_addons.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobMilitary.h"
#include "factories/BuildingFactory.h"
#include "figures/nofPassiveSoldier.h"
#include "worldFixtures/WorldWithGCExecution.h"
#include "world/BuildingRanges.h"
#include "gameData/MilitaryConsts.h"
#include "gameData/SettingTypeConv.h"
#include <boost/test/unit_test.hpp>
#include <optional>

// LCOV_EXCL_START
static std::ostream& operator<<(std::ostream& out, const BuildingRangeKind kind)
{
    return out << static_cast<unsigned>(kind);
}
static std::ostream& operator<<(std::ostream& out, const std::optional<BuildingRangeKind>& kind)
{
    if(!kind)
        return out << "none";
    return out << *kind;
}
// LCOV_EXCL_STOP

BOOST_AUTO_TEST_SUITE(BuildingRangesSuite)

namespace {
const std::optional<BuildingRangeKind> noRange;

struct BuildingRangesFixture : public WorldWithGCExecution<1, 80, 40>
{
    BuildingRangesFixture()
    {
        // Attack with all soldiers
        ChangeMilitary(MILITARY_SETTINGS_SCALE);
    }

    MapPoint East(MapPoint pt, unsigned numSteps) const
    {
        for(unsigned i = 0; i < numSteps; i++)
            pt = world.GetNeighbour(pt, Direction::East);
        return pt;
    }

    nobMilitary& CreateOccupiedMilitaryBld(BuildingType bldType, MapPoint pos, unsigned numSoldiers)
    {
        auto* bld = dynamic_cast<nobMilitary*>(BuildingFactory::CreateBuilding(world, bldType, pos, 0, Nation::Romans));
        BOOST_TEST_REQUIRE(bld);
        for(unsigned i = 0; i < numSoldiers; i++)
        {
            auto& soldier = world.AddFigure(pos, std::make_unique<nofPassiveSoldier>(pos, 0, bld, bld, 0));
            world.GetPlayer(0).IncreaseInventoryJob(soldier.GetJobType(), 1);
            // Let him "walk" to goal -> Already reached -> Added and all internal states set correctly
            soldier.WalkToGoal();
        }
        BOOST_TEST_REQUIRE(bld->GetNumTroops() == numSoldiers);
        BOOST_TEST_REQUIRE(!bld->IsNewBuilt());
        return *bld;
    }

    std::optional<BuildingRangeKind> GetKind(const noBaseBuilding& bld, MapPoint pt) const
    {
        return BuildingRanges(world, bld).GetKind(pt);
    }
};
} // namespace

BOOST_FIXTURE_TEST_CASE(WorkRangeOfForester, BuildingRangesFixture)
{
    const MapPoint bldPos = East(hqPos, 4);
    const noBaseBuilding* bld =
      BuildingFactory::CreateBuilding(world, BuildingType::Forester, bldPos, 0, Nation::Romans);

    BOOST_TEST(GetKind(*bld, bldPos) == BuildingRangeKind::Work);
    BOOST_TEST(GetKind(*bld, East(bldPos, 6)) == BuildingRangeKind::Work);
    BOOST_TEST(GetKind(*bld, East(bldPos, 7)) == noRange);
    BOOST_TEST(!BuildingRanges(world, *bld).HasTerritoryChange());

    // Radius is adjusted by the addon
    ggs.setSelection(AddonId::FORESTER_REACH_RADIUS, 1);
    BOOST_TEST(GetKind(*bld, East(bldPos, 8)) == BuildingRangeKind::Work);
    BOOST_TEST(GetKind(*bld, East(bldPos, 9)) == noRange);
}

BOOST_FIXTURE_TEST_CASE(RangeOfCatapult, BuildingRangesFixture)
{
    const MapPoint bldPos = East(hqPos, 4);
    const noBaseBuilding* bld =
      BuildingFactory::CreateBuilding(world, BuildingType::Catapult, bldPos, 0, Nation::Romans);

    BOOST_TEST(GetKind(*bld, East(bldPos, CATAPULT_RANGE)) == BuildingRangeKind::Attack);
    BOOST_TEST(GetKind(*bld, East(bldPos, CATAPULT_RANGE + 1)) == noRange);
}

BOOST_FIXTURE_TEST_CASE(RangesOfMilitaryBuilding, BuildingRangesFixture)
{
    // Barracks at the border of the HQ territory
    const MapPoint bldPos = East(hqPos, 7);
    nobMilitary& bld = CreateOccupiedMilitaryBld(BuildingType::Barracks, bldPos, 1);
    const unsigned radius = MILITARY_RADIUS[0];

    const BuildingRanges ranges(world, bld);
    BOOST_TEST(!ranges.IsTerritoryGain());
    BOOST_TEST(ranges.HasTerritoryChange());
    // Land only held by the barracks is lost
    BOOST_TEST(ranges.GetKind(East(bldPos, radius - 1)) == BuildingRangeKind::TerritoryChange);
    // Land also held by the HQ is kept
    BOOST_TEST(ranges.GetKind(hqPos) == BuildingRangeKind::Territory);
    BOOST_TEST(ranges.GetKind(East(bldPos, radius + 3)) == BuildingRangeKind::Defense);
    BOOST_TEST(ranges.GetKind(East(bldPos, MAX_AGGRESSIVE_DEFENDER_DISTANCE)) == BuildingRangeKind::Defense);
    // With only 1 soldier nobody can attack
    BOOST_TEST(ranges.GetKind(East(bldPos, MAX_AGGRESSIVE_DEFENDER_DISTANCE + 1)) == noRange);
    BOOST_TEST(bld.GetMaxAttackDistance() == 0u);

    // A second soldier can attack
    world.AddFigure(bldPos, std::make_unique<nofPassiveSoldier>(bldPos, 0, &bld, &bld, 0)).WalkToGoal();
    BOOST_TEST_REQUIRE(bld.GetNumTroops() == 2u);
    BOOST_TEST(bld.GetMaxAttackDistance() == BASE_ATTACKING_DISTANCE);
    BOOST_TEST(GetKind(bld, East(bldPos, MAX_AGGRESSIVE_DEFENDER_DISTANCE + 1)) == BuildingRangeKind::Attack);
    BOOST_TEST(GetKind(bld, East(bldPos, BASE_ATTACKING_DISTANCE)) == BuildingRangeKind::Attack);
    BOOST_TEST(GetKind(bld, East(bldPos, BASE_ATTACKING_DISTANCE + 1)) == noRange);
}

BOOST_FIXTURE_TEST_CASE(AttackDistanceMatchesAvailableSoldiers, BuildingRangesFixture)
{
    BOOST_TEST(nobMilitary::GetMaxAttackDistance(0) == 0u);
    for(unsigned numAttackers = 1; numAttackers < 10; numAttackers++)
    {
        const unsigned maxDistance = nobMilitary::GetMaxAttackDistance(numAttackers);
        BOOST_TEST(maxDistance == BASE_ATTACKING_DISTANCE + (numAttackers - 1) * EXTENDED_ATTACKING_DISTANCE);
    }

    const MapPoint bldPos = East(hqPos, 7);
    nobMilitary& bld = CreateOccupiedMilitaryBld(BuildingType::Fortress, bldPos, 9);
    // 1 soldier stays at home
    BOOST_TEST(bld.GetMaxAttackDistance() == nobMilitary::GetMaxAttackDistance(8));
    // Attack setting reduces the number of attackers
    MilitarySettings milSettings = MILITARY_SETTINGS_SCALE;
    milSettings[3] = MILITARY_SETTINGS_SCALE[3] / 2;
    ChangeMilitary(milSettings);
    const unsigned numAttackers = 8u * milSettings[3] / MILITARY_SETTINGS_SCALE[3];
    BOOST_TEST_REQUIRE(numAttackers < 8u);
    BOOST_TEST(bld.GetMaxAttackDistance() == nobMilitary::GetMaxAttackDistance(numAttackers));
}

BOOST_FIXTURE_TEST_CASE(MilitaryBuildingWithoutOwnTerritory, BuildingRangesFixture)
{
    // Next to the HQ the HQ holds all the land of the barracks
    const MapPoint bldPos = world.GetNeighbour(hqPos, Direction::West);
    nobMilitary& bld = CreateOccupiedMilitaryBld(BuildingType::Barracks, bldPos, 1);

    const BuildingRanges ranges(world, bld);
    BOOST_TEST(!ranges.IsTerritoryGain());
    BOOST_TEST(!ranges.HasTerritoryChange());
    BOOST_TEST(ranges.GetKind(bldPos) == BuildingRangeKind::Territory);
}

BOOST_FIXTURE_TEST_CASE(RangesOfMilitaryBuildingSite, BuildingRangesFixture)
{
    const MapPoint bldPos = East(hqPos, 6);
    BOOST_TEST_REQUIRE(SetBuildingSite(bldPos, BuildingType::Guardhouse));
    const auto* site = world.GetSpecObj<noBuildingSite>(bldPos);
    BOOST_TEST_REQUIRE(site);
    const unsigned radius = MILITARY_RADIUS[1];

    const BuildingRanges ranges(world, *site);
    BOOST_TEST(ranges.IsTerritoryGain());
    BOOST_TEST(ranges.HasTerritoryChange());
    // Gains land outside the HQ territory
    BOOST_TEST(ranges.GetKind(East(bldPos, radius - 1)) == BuildingRangeKind::TerritoryChange);
    // Land of the HQ is not gained
    BOOST_TEST(ranges.GetKind(hqPos) == BuildingRangeKind::Territory);
    // Attack range for the full garrison: 3 soldiers of which 2 may attack
    const unsigned attackDistance = nobMilitary::GetMaxAttackDistance(2);
    BOOST_TEST(ranges.GetKind(East(bldPos, attackDistance)) == BuildingRangeKind::Attack);
    BOOST_TEST(ranges.GetKind(East(bldPos, attackDistance + 1)) == noRange);
}

BOOST_FIXTURE_TEST_CASE(WorkRangeOfBuildingSite, BuildingRangesFixture)
{
    const MapPoint bldPos = East(hqPos, 4);
    BOOST_TEST_REQUIRE(SetBuildingSite(bldPos, BuildingType::Woodcutter));
    const auto* site = world.GetSpecObj<noBuildingSite>(bldPos);
    BOOST_TEST_REQUIRE(site);

    const BuildingRanges ranges(world, *site);
    BOOST_TEST(!ranges.HasTerritoryChange());
    BOOST_TEST(ranges.GetKind(East(bldPos, 6)) == BuildingRangeKind::Work);
    BOOST_TEST(ranges.GetKind(East(bldPos, 7)) == noRange);
}

BOOST_AUTO_TEST_SUITE_END()
