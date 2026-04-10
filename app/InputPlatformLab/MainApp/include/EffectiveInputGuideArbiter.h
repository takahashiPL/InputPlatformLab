// T76: single-player effective input owner / guide family vs device inventory (future: per PlayerSlot[i], T77).
#pragma once

#include "GamepadTypes.h"
#include "VirtualInputMenuSample.h"

// Design cap for a future PlayerSlot table (this build uses slot 0 only).
constexpr unsigned kInputGuideArbiterMaxPlayerSlotsCap = 8u;

// What the UI and guide labels treat as the active input source (not the device enumeration order).
enum class InputGuideSourceKind : UINT8
{
    Unknown = 0,
    Keyboard,
    Gamepad,
};

// Called when T18 completes a device inventory refresh (enumeration). Does not change effective owner.
void InputGuideArbiter_OnDeviceInventoryRefreshed(
    GameControllerKind inventoryGamepadFamily,
    bool hidGamepadFound,
    int xinputSlot);

// Timer frame: keyboard vs pad consumer frames (pre-merge). Updates owner from meaningful activity only.
// gamepadGuideFamilyHintOnActivity: VirtualInputSnapshot.family when the pad frame has meaningful activity;
// otherwise Unknown (do not refresh latch from inventory-only paths).
void InputGuideArbiter_TickSinglePlayerFromConsumerFrames(
    const VirtualInputConsumerFrame& keyboardFrame,
    const VirtualInputConsumerFrame& gamepadFrame,
    GameControllerKind gamepadGuideFamilyHintOnActivity);

InputGuideSourceKind InputGuideArbiter_GetEffectiveOwnerSourceKind();
// Keyboard owner: Unknown. Gamepad owner: latched activity family if set, else inventory fallback.
GameControllerKind InputGuideArbiter_GetEffectiveGuideFamilyForUi();
