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

// T77 step21: live consume trial target sentinel (valid slots are 0..kPlayerInputSlotCap-1).
constexpr PlayerInputSlotIndex kPlayerInputNoLiveConsumeTrialTarget =
    static_cast<PlayerInputSlotIndex>(255u);

// T77 step2: physical / logical identity for what a slot may bind to (routing still step3+).
enum class PlayerBoundDeviceIdentityKind : UINT8
{
    Unknown = 0,
    Unbound, // slot active but no device instance locked (1P step2 default)
    Keyboard,
    XInputUser,
    HidPathHash,
};

// T77 step2: party-seat vs device-lock policy for a slot (not input routing).
enum class PlayerSlotBindingAssignment : UINT8
{
    None = 0, // 2P+ default: no seat / no binding policy
    ActiveOpen, // 1P today: seat in use; device binding open until step3/explicit bind
    BoundLocked, // future: locked to a specific source instance (rebind UI)
};

// T77 step13: whether this slot may drive app menu/HUD (v1: only slot0 may be Live; slot1+ DryRun/Disabled).
enum class PlayerSlotActualConsumePolicy : UINT8
{
    Disabled = 0,
    DryRun,
    Live,
};

enum class PlayerSlotConsumePolicySource : UINT8
{
    DefaultStep13Seed = 0,
    ManualOverride, // T77 step14: Refresh skips; actualConsumePolicy holds override value
};

// T77 step4: binding policy vs current inventory (T18) — display / future routing only.
enum class PlayerSlotBindingResolveStatus : UINT8
{
    Unresolved = 0,
    IdleNoPolicy,
    OpenNoLock,
    LockedPresent,
    LockedAbsent,
};

// T77 step5: route readiness (candidate only; no per-slot input plumbing yet).
enum class PlayerSlotRouteReadiness : UINT8
{
    None = 0, // no adoptable route (idle policy, locked-absent, or unresolved)
    OpenReady, // ActiveOpen: soft inventory-aligned candidate; not committed to routing
    Ready, // BoundLocked + present: candidate matches binding and inventory
};

// Why this candidate exists (for debug / future routing).
enum class PlayerSlotRouteCandidateMode : UINT8
{
    None = 0,
    OpenSoftInventory, // mirror primary inventory (pad if any, else keyboard)
    LockedKeyboard,
    LockedXInputUser,
    LockedHidPath,
};

// T77 step6: active branch for slot route (slot0 only wired; same consumer merge as pre-step6 for 1P).
enum class PlayerSlotActiveRouteMode : UINT8
{
    NoRoute = 0,
    OpenSoft,
    LockedKeyboard,
    LockedXinput,
    LockedHid,
};
