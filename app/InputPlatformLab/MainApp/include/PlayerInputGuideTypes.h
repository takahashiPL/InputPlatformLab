// T77: shared guide / owner enums and player-slot sizing (T76 arbiter uses slot 0 only for now).
#pragma once

#include "CommonTypes.h"

// UI / arbitration: which input source owns guide labels for a player slot.
enum class InputGuideSourceKind : UINT8
{
    Unknown = 0,
    Keyboard,
    Gamepad,
};

// Default party size for local multi-player; array may be sized to kPlayerInputSlotCap.
constexpr unsigned kPlayerInputSlotCountDefault = 4u;
// Hard cap for static storage / future 8P extension.
constexpr unsigned kPlayerInputSlotCap = 8u;

// Local player slot id: 0 = 1P, 1 = 2P, ... (must stay within kPlayerInputSlotCap).
using PlayerInputSlotIndex = UINT8;
static_assert(kPlayerInputSlotCap <= 256u, "PlayerInputSlotIndex must cover all slots");
