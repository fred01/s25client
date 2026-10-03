// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "AddonBool.h"

class AddonSettlersUseWaterways : public AddonBool
{
public:
    AddonSettlersUseWaterways()
        : AddonBool(AddonId::SETTLERS_USE_WATERWAYS, AddonGroup::Economy | AddonGroup::GamePlay,
                    _("Settlers use waterways"), _("Settlers can travel along waterways by boat, not only wares."))
    {}
};
