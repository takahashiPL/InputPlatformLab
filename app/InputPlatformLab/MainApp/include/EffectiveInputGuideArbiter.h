// T76: effective input owner / guide family vs device inventory. T77: state lives in PlayerSlotState[0] (1P only for now).
#pragma once

#include "PlayerInputSlots.h"
#include "VirtualInputMenuSample.h"

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

// T77 step2: T18 diagnostic lines for slot0 binding policy (no routing; slot1+ unchanged).
void InputGuideArbiter_FormatPrimarySlotBoundSourceForT18(wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatPrimarySlotBoundDeviceIdentityForT18(wchar_t* buf, size_t bufCount);
