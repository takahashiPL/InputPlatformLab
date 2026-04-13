// T77: shared guide / owner enums and player-slot sizing (portable). Layer map: docs/architecture.md「入力 foundation の整理」.
// T76 arbiter drives slot 0 for live app consume; higher slots carry policy/staging for trials / future MP.
#pragma once

#include "CommonTypes.h"

// InputGuideSourceKind — who owns on-screen guide/button glyphs for a slot (HUD arbitration).
// Granularity: coarse source class only (no per-device id). Intended to stay backend-agnostic.
// Default in fresh state: Unknown. Not Debug-only.
enum class InputGuideSourceKind : UINT8
{
    Unknown = 0, // not classified yet; safe initial / idle label
    Keyboard, // keyboard-originated guide family
    Gamepad, // gamepad-originated (any pad family buckets elsewhere)
};

// Default party size for local multi-player demos; not a hard cap (see kPlayerInputSlotCap).
constexpr unsigned kPlayerInputSlotCountDefault = 4u;

// kPlayerInputSlotCap — static array size for PlayerSlotState[kPlayerInputSlotCap] and related tables.
// Why this value: fits UINT8 slot indices, leaves headroom beyond 4P without huge static waste.
// Not Debug-only; changing it touches static storage across the arbiter.
constexpr unsigned kPlayerInputSlotCap = 8u;

// PlayerInputSlotIndex —0 = 1P, 1 = 2P, … (must be < kPlayerInputSlotCap).
// Sentinel values must not collide with real slots; see kPlayerInputNoLiveConsumeTrialTarget.
using PlayerInputSlotIndex = UINT8;
static_assert(kPlayerInputSlotCap <= 256u, "PlayerInputSlotIndex must cover all slots");

// kPlayerInputNoLiveConsumeTrialTarget — reserved slot index meaning "no trial row selected" (trg=--).
// Why 255: valid slots are 0..kPlayerInputSlotCap-1; 255 avoids overlap with real seats.
// Debug/UI: F11 (_DEBUG) can set this; live resolver then stays on slot0 when armed.
// Not a runtime "player"; do not use as dispatch slot.
constexpr PlayerInputSlotIndex kPlayerInputNoLiveConsumeTrialTarget =
    static_cast<PlayerInputSlotIndex>(255u);

// PlayerBoundDeviceIdentityKind — what device instance a slot claims (policy + inventory resolution).
// Granularity: project-specific identity kinds (XInput user index, HID hash); portable code treats as opaque.
enum class PlayerBoundDeviceIdentityKind : UINT8
{
    Unknown = 0,
    Unbound, // seat active but no device instance locked (typical 1P seed)
    Keyboard, // logical keyboard identity for this slot
    XInputUser, // Windows XInput user slot0..3 when bound
    HidPathHash, // HID instance path hashed (see PlayerBoundDeviceIdentity)
};

// PlayerSlotBindingAssignment — seat policy: is this slot participating, and is the bind fixed?
// Not routing: describes intent only. Default for unused seats: None. 1P seed: ActiveOpen.
enum class PlayerSlotBindingAssignment : UINT8
{
    None = 0, // no seat policy (typical unassigned 2P+)
    ActiveOpen, // seat in use; binding open until explicit lock / demo bind
    BoundLocked, // locked to a specific source instance (trial / future rebind)
};

// PlayerSlotActualConsumePolicy — may this slot apply menu/HUD consume to real app state?
// Granularity: tri-state per slot. Default seed: slot0 Live; assigned non-primary DryRun; else Disabled.
// Manual Live on non-primary is Debug/trial path only unless product wiring changes.
enum class PlayerSlotActualConsumePolicy : UINT8
{
    Disabled = 0, // no staging consume; skip dispatch
    DryRun, // run scratch apply / staging only (slot1+ default when assigned)
    Live, // may mutate app menu / primary consume path when selected as single live slot
};

// PlayerSlotConsumePolicySource — whether policy comes from seat seed or explicit override.
// ManualOverride: refresh from seat flags does not clobber (step14); used for F9 trial and tests.
enum class PlayerSlotConsumePolicySource : UINT8
{
    DefaultStep13Seed = 0, // derived from seat/binding flags on refresh
    ManualOverride, // holds actualConsumePolicy until cleared
};

// PlayerSlotBindingResolveStatus — inventory (T18) vs declared bind: is the device there?
// Display + future routing only; does not move OS input by itself.
enum class PlayerSlotBindingResolveStatus : UINT8
{
    Unresolved = 0, // not computed this tick
    IdleNoPolicy, // None assignment: nothing to resolve
    OpenNoLock, // ActiveOpen: no locked device to match
    LockedPresent, // locked bind satisfied by inventory
    LockedAbsent, // locked bind not satisfied (disconnected / mismatch)
};

// PlayerSlotRouteReadiness — can we build a route candidate from bind + inventory?
enum class PlayerSlotRouteReadiness : UINT8
{
    None = 0, // no adoptable route (idle policy, locked-absent, unresolved)
    OpenReady, // ActiveOpen: soft inventory-aligned candidate; not committed
    Ready, // BoundLocked + present: firm candidate
};

// PlayerSlotRouteCandidateMode — why the route candidate exists (debug labels / future router).
enum class PlayerSlotRouteCandidateMode : UINT8
{
    None = 0,
    OpenSoftInventory, // mirror inventory (pad if any, else keyboard)
    LockedKeyboard,
    LockedXInputUser,
    LockedHidPath,
};

// PlayerSlotActiveRouteMode — which branch staged input uses for this slot (step6+).
// slot0: wired to live merge/tick; slot1+: dry-run staging. Names align with candidate modes.
enum class PlayerSlotActiveRouteMode : UINT8
{
    NoRoute = 0,
    OpenSoft,
    LockedKeyboard,
    LockedXinput, // mirrors LockedXInputUser candidate
    LockedHid,
};
