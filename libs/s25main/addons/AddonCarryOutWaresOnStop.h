// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "AddonBool.h"

class AddonCarryOutWaresOnStop : public AddonBool
{
public:
    AddonCarryOutWaresOnStop()
        : AddonBool(AddonId::CARRY_OUT_WARES_ON_STOP, AddonGroup::Economy,
                    _("Carry out wares when production is stopped"),
                    _("When production of a building is stopped, its worker carries all stored input wares out of the "
                      "building and wares still on their way to it are redirected."))
    {}
};
