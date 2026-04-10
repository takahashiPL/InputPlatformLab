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
void InputGuideArbiter_TickSinglePlayerFromConsumerFrames(
    const VirtualInputConsumerFrame& keyboardFrame,
    const VirtualInputConsumerFrame& gamepadFrame);

InputGuideSourceKind InputGuideArbiter_GetEffectiveOwnerSourceKind();
// For Gamepad owner: follows refreshed inventory family. For Keyboard owner: Unknown (generic keyboard-oriented labels).
GameControllerKind InputGuideArbiter_GetEffectiveGuideFamilyForUi();
