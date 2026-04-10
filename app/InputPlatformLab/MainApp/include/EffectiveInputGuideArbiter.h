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

// T77 step2/3: T18 diagnostic lines for binding policy (no routing).
void InputGuideArbiter_FormatPrimarySlotBoundSourceForT18(wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatPrimarySlotBoundDeviceIdentityForT18(wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatSlotBoundSourceForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatSlotBoundDeviceIdentityForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);

bool InputGuideArbiter_IsValidSlotIndex(PlayerInputSlotIndex slot);
void InputGuideArbiter_SetSlotBindingAssignment(PlayerInputSlotIndex slot, PlayerSlotBindingAssignment assignment);
void InputGuideArbiter_SetSlotBoundSourceKind(PlayerInputSlotIndex slot, InputGuideSourceKind kind);
void InputGuideArbiter_SetSlotBoundDeviceIdentity(PlayerInputSlotIndex slot, const PlayerBoundDeviceIdentity& identity);
void InputGuideArbiter_SetSlotPreferredGuideFamily(PlayerInputSlotIndex slot, GameControllerKind family);
// Reserved party seat (step3: slot1+ may be assigned but not input-active until routing).
void InputGuideArbiter_SetSlotPartySeatFlags(PlayerInputSlotIndex slot, bool assigned, bool active);
void InputGuideArbiter_ClearSlotBindingPolicy(PlayerInputSlotIndex slot);
void InputGuideArbiter_BindSlotToKeyboard(PlayerInputSlotIndex slot);
void InputGuideArbiter_BindSlotToXInputUser(PlayerInputSlotIndex slot, INT32 xinputUserIndex);
void InputGuideArbiter_BindSlotToHidPathHash(
    PlayerInputSlotIndex slot,
    UINT32 hidPathHash,
    UINT16 hidPathShortToken,
    UINT16 vendorId,
    UINT16 productId);

// Demo: slot0 stays ActiveOpen; slot1 keyboard; slot2 XInput user 0. Policy/display only.
void InputGuideArbiter_ApplyStep3DemoReservationBindings();
