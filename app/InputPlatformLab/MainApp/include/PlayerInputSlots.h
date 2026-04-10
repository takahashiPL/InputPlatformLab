// T77: per-player slot state (vessel). Only slot 0 is driven by T76 today; slots 1.. can carry binding policy only.
//
// --- T77 step2/3 assignment / binding policy (convention; no auto-assign; no routing in step3) ---
// - Device enumeration / connection list (T18) and per-slot binding are separate concerns.
// - Hot-plug or replacing one physical device must not rewrite other slots' bindings (no cascade).
// - Keyboard is a single logical source type: at most one "keyboard" bind target per slot when bound.
// - No auto-assign: nothing in this module assigns devices to slots from the connection list.
// - No rebind UI / persistence yet: bindings are runtime-only unless code calls the arbiter setters.
// - Step3: binding policy can be set per slot (open / locked / none); input is still merged for 1P / T76 only.
#pragma once

#include "GamepadTypes.h"
#include "PlayerInputGuideTypes.h"

// Instance key for a bound device (step2: placeholder fields; step3+ fills from backends).
struct PlayerBoundDeviceIdentity
{
    PlayerBoundDeviceIdentityKind kind = PlayerBoundDeviceIdentityKind::Unknown;
    // XInputUser:0..3 when kind == XInputUser; otherwise ignore.
    INT32 xinputUserIndex = -1;
    // HidPathHash: FNV-1a (or project hash) over normalized device path; 0 = unset.
    UINT32 hidInstancePathHash = 0;
    // Short token for compact HUD (e.g. low bits of hash); optional.
    UINT16 hidPathShortToken = 0;
    UINT16 vendorId = 0;
    UINT16 productId = 0;
};

// Last observed activity metadata for arbitration / future multi-slot (step2: observability only on slot0 tick).
struct PlayerSlotLastSeenSourceMeta
{
    InputGuideSourceKind sourceKind = InputGuideSourceKind::Unknown;
    PlayerBoundDeviceIdentityKind deviceKind = PlayerBoundDeviceIdentityKind::Unknown;
    UINT32 atTick = 0;
};

// One local player slot: binding (T77 step2), effective owner/guide (T76 on slot 0), inventory mirror, debounce.
struct PlayerSlotState
{
    bool slotAssigned = false;
    bool slotActive = false;

    PlayerSlotBindingAssignment bindingAssignment = PlayerSlotBindingAssignment::None;
    // Declared source class when BoundLocked / future UI; Unknown under ActiveOpen means "any".
    InputGuideSourceKind boundSourceKind = InputGuideSourceKind::Unknown;
    PlayerBoundDeviceIdentity boundDeviceIdentity;
    GameControllerKind preferredGuideFamily = GameControllerKind::Unknown;
    PlayerSlotLastSeenSourceMeta lastSeenSourceMeta;

    // T76 effective state (maps to slot 0 / 1P).
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
