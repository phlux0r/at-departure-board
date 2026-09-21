#pragma once
#include "live.h"

// The watches the board seeds NVS with on its FIRST boot, and only then:
// after that the setup page at http://<board-ip>/ owns the config (spec 3).
// Edit these only to change what a freshly flashed board starts life with.
// Stop codes are the numbers on the pole; nothing here is secret.
//
// route_short_name "" means any route, which is what carries a rail watch
// through a line rename - see docs/at-api-notes.md on the CRL changeover.
// toward_stop_code is the stop you are travelling toward, never a direction:
// direction is derived at every refresh (spec 3a).
static const char* const LOCATION = "Chatswood";

static const WatchConfig WATCHES[] = {
    {"to Aon Centre", "4237", "931", "7142"},
    {"to Aon Centre", "4351", "97R", "7142"},  // no macron: the panel font is ASCII
};
static constexpr int N_WATCHES = sizeof WATCHES / sizeof WATCHES[0];
