// T77: per-player slot state (vessel). Only slot 0 is driven by T76 today; slots 1.. remain default.
#pragma once

#include "GamepadTypes.h"
#include "PlayerInputGuideTypes.h"

// Placeholder until device routing / assignment (T77+). VID/PID optional for debug.
struct PlayerBoundDeviceIdentityPlaceholder
{
    bool hasIdentity = false;
    UINT16 vendorId = 0;
    UINT16 productId = 0;
};

// One local player slot: binding (future), effective owner/guide (T76 on slot 0), inventory mirror, debounce.
struct PlayerSlotState
{
    bool slotAssigned = false;
    bool slotActive = false;

    // Future: which hardware class is bound to this slot (keyboard vs gamepad instance, etc.).
    InputGuideSourceKind boundSourceKind = InputGuideSourceKind::Unknown;
    PlayerBoundDeviceIdentityPlaceholder boundDevice;

    // T76 effective state (was single-player g_sp; maps to slot 0 / 1P).
    InputGuideSourceKind effectiveOwnerSource = InputGuideSourceKind::Unknown;
    GameControllerKind inventoryPadFamily = GameControllerKind::Unknown;
    bool inventoryPadPresent = false;
    GameControllerKind latchedGamepadGuideFamily = GameControllerKind::Unknown;

    InputGuideSourceKind pendingOwnerIdeal = InputGuideSourceKind::Unknown;
    UINT32 pendingOwnerSinceTick = 0;

    // Debug log dedupe (T76)
    InputGuideSourceKind logPrevOwner = InputGuideSourceKind::Unknown;
    GameControllerKind logPrevGuide = GameControllerKind::Unknown;
    GameControllerKind logPrevInventoryFamily = GameControllerKind::Unknown;
    bool logPrevInventoryPresent = false;
    UINT32 lastInventoryGuideKeptLogTick = 0;

    // Reserved for last-active / lock / multi-device arbitration (T77+).
    UINT32 reservedArbitrationU32[2] = {};
};
