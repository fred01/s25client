// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/MapCoordinates.h"
#include <cstdint>
#include <optional>
#include <vector>

class GameWorldBase;
class noBaseBuilding;

/// Kind of a range of a building. Ordered by priority: If ranges overlap, the higher one is shown
enum class BuildingRangeKind : uint8_t
{
    Work,           /// Range in which the worker works (e.g. forester, woodcutter, fisher)
    Attack,         /// Enemy buildings in this range can be attacked (by soldiers or a catapult)
    Defense,        /// Attackers in this range are intercepted by soldiers of the building
    Territory,      /// Territory radius of a building holding land
    TerritoryChange /// Territory lost when the building is destroyed or gained when it is occupied
};
constexpr auto maxEnumValue(BuildingRangeKind)
{
    return BuildingRangeKind::TerritoryChange;
}

/// The ranges of a building or building site, which can be visualized on the map
class BuildingRanges
{
public:
    BuildingRanges(const GameWorldBase& world, const noBaseBuilding& building);

    /// Return the range with the highest priority containing the point
    std::optional<BuildingRangeKind> GetKind(MapPoint pt) const;
    /// Return whether the territory would change by destroying the building (or by occupying it, see IsTerritoryGain)
    bool HasTerritoryChange() const { return !territoryChange.empty(); }
    /// True if the territory change is the gain when the building gets occupied instead of the loss on destroying it
    bool IsTerritoryGain() const { return isTerritoryGain; }

private:
    struct Range
    {
        MapPoint center;
        unsigned radius;
        BuildingRangeKind kind;
    };

    void AddRange(MapPoint center, unsigned radius, BuildingRangeKind kind);
    void CalcTerritoryChange(const noBaseBuilding& building, unsigned previewRadius);

    const GameWorldBase& world;
    /// Ranges sorted by descending priority
    std::vector<Range> ranges;
    /// Sorted (MapPointLess) points whose owner changes
    std::vector<MapPoint> territoryChange;
    bool isTerritoryGain = false;
};
