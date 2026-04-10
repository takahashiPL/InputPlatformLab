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

// T77 step6: slot0 generic route entry (updates activeRoute* then delegates to T76 tick; same kb/pad frames).
void InputGuideArbiter_TickSlot0GenericRouteFromConsumerFrames(
    const VirtualInputConsumerFrame& keyboardFrame,
    const VirtualInputConsumerFrame& gamepadFrame,
    GameControllerKind gamepadGuideFamilyHintOnActivity);

// T77 step8: after unified merge, stage per-slot consumer frames from activeRoute (dry-run for slot1+; slot0 = live triple).
void InputGuideArbiter_StagePerSlotInputFramesDryFanOut(
    const VirtualInputConsumerFrame& keyboardFrame,
    const VirtualInputConsumerFrame& gamepadFrame,
    const VirtualInputConsumerFrame& mergedUnifiedLive);

// T77 step9: after step8, stage per-slot logical + action snapshot (slot0 = live mirror; slot1+ from staged consumer only).
void InputGuideArbiter_StagePerSlotLogicalDryFanOut();

// T77 step10: after app LogicalInputState_Update, mirror live into slot0 staged (T19/timer reads consume path). Idempotent with step9 slot0 copy.
void InputGuideArbiter_SyncSlot0StagedLogicalMirrorFromLivePrimary();

// T77 step10/11: slot0 staged mirror (before merge) + per-slot dispatch getters (step11: slot1+ staged readable; live consume only slot0).
// TryGet merged: only after step8 in the same tick. Slot0 logical: fallback InputCore if unstaged. Slot1+ logical: nullptr if unstaged.
bool InputGuideArbiter_CanSlotDispatchLiveConsume(PlayerInputSlotIndex slot);
bool InputGuideArbiter_ShouldSlotDispatchDryRunConsume(PlayerInputSlotIndex slot);
PlayerSlotActualConsumePolicy InputGuideArbiter_GetSlotActualConsumePolicy(PlayerInputSlotIndex slot);
const LogicalInputState* InputGuideArbiter_GetSlotStagedLogicalForDispatch(PlayerInputSlotIndex slot);
const VirtualInputConsumerFrame* InputGuideArbiter_TryGetSlotStagedMergedForDispatch(PlayerInputSlotIndex slot);
const PlayerSlotStagedActionSnapshot* InputGuideArbiter_TryGetSlotStagedActionForDispatch(PlayerInputSlotIndex slot);

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

// T77 step4: refresh bindingResolution from T18/inventory view (after OnDeviceInventoryRefreshed). No routing.
void InputGuideArbiter_ResolveSlotBindingsFromInventory(const PlayerInputInventoryBindingView& inventory);
void InputGuideArbiter_FormatSlotBindStatusForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatSlotBindMatchForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);

// T77 step5: compact route-candidate label (no routing).
void InputGuideArbiter_FormatSlotRouteCandidateForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);

// T77 step6/7: active route labels. Slot index !=0 appends (dry-run) on mode line; input still slot0 only.
void InputGuideArbiter_FormatSlotActiveRouteModeForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatSlotRoutedSourceForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatSlot0ActiveRouteModeForT18(wchar_t* buf, size_t bufCount);
void InputGuideArbiter_FormatSlot0RoutedSourceForT18(wchar_t* buf, size_t bufCount);

// T77 step8: compact staged-input label (follows activeRoute; slot0 = live unified).
void InputGuideArbiter_FormatSlotStagedInputSummaryForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);

// T77 step9: compact staged-logical label (after staged input in T18).
void InputGuideArbiter_FormatSlotStagedLogicalSummaryForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);

// T77 step13: seeded consume policy label (replaces step11 dispatch line).
void InputGuideArbiter_FormatSlotConsumePolicyForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);

// T77 step12: record last consume outcome (MainApp dispatch loop). Dry-run does not mutate app menu state.
void InputGuideArbiter_RecordSlotConsumeDispatchSkipped(PlayerInputSlotIndex slot, UINT32 tick);
void InputGuideArbiter_RecordSlotConsumeDispatchLive(PlayerInputSlotIndex slot, UINT32 tick);
void InputGuideArbiter_RecordSlotConsumeDispatchDryRun(
    PlayerInputSlotIndex slot,
    const VirtualInputMenuSampleEvents& ev,
    UINT32 tick);
void InputGuideArbiter_FormatSlotConsumeResultForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount);
