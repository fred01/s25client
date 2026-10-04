// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "BuildingRangeHighlight.h"
#include "IngameWindow.h"

BuildingRangeHighlight::BuildingRangeHighlight(GameWorldView& gwv, const IngameWindow& window,
                                               const noBaseBuilding& building)
    : gwv(gwv), window(window), building(building)
{
    gwv.AddBuildingRangeSource(this);
}

BuildingRangeHighlight::~BuildingRangeHighlight()
{
    gwv.RemoveBuildingRangeSource(this);
}

const noBaseBuilding* BuildingRangeHighlight::GetRangeBuilding() const
{
    // A window queued for closing might belong to an already destroyed building
    if(window.IsMinimized() || window.ShouldBeClosed())
        return nullptr;
    return &building;
}
