// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "world/GameWorldView.h"
#include <boost/noncopyable.hpp>

class IngameWindow;
class noBaseBuilding;

/// Shows the ranges of the building of a window on the map while the window is open and not minimized
class BuildingRangeHighlight : public IBuildingRangeSource, private boost::noncopyable
{
public:
    BuildingRangeHighlight(GameWorldView& gwv, const IngameWindow& window, const noBaseBuilding& building);
    ~BuildingRangeHighlight() override;

    const noBaseBuilding* GetRangeBuilding() const override;

private:
    GameWorldView& gwv;
    const IngameWindow& window;
    const noBaseBuilding& building;
};
