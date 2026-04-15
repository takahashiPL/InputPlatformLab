// Input/core: effective guide owner + per-slot consume policy vs device inventory (see PlayerInputSlots.h).
// Pack-out: header + logic are reusable candidates; .cpp uses Windows.h for tick/debug — replace when porting OS.
// Portable API surface; keep new OS calls out of headers.
// T76: slot0 drives live owner; T77: multi-slot policy/staging + single live consume + Debug trial hooks.
#pragma once

#include "PlayerInputSlots.h"
#include "VirtualInputMenuSample.h"

// =============================================================================
// owner / guide preference state
// =============================================================================

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

// =============================================================================
// binding resolution / presence evaluation
// =============================================================================

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
// Step17/21/22: ·tr= only when lineSlot == configured trg (trg=-- → empty on all lines).
void InputGuideArbiter_FormatLiveTrialObsForT18(PlayerInputSlotIndex lineSlot, wchar_t* buf, size_t bufCount);

// =============================================================================
// public update / query entry points
// =============================================================================

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
// T77 step19–21: exactly one live consume slot per tick (GetSingleLiveConsumeSlotIndex: 0 default;
// non-zero only when trial armed + Manual Live + kb route on GetLiveConsumeTrialTargetSlot()).
// step20: Debug-only change logs align lv=/tr=/dispatch. step21: slot-indexed trial target (default 2P).
// TryGet merged: only after step8 in the same tick. Slot0 logical: fallback InputCore if unstaged. Slot1+ logical: nullptr if unstaged.
bool InputGuideArbiter_CanSlotDispatchLiveConsume(PlayerInputSlotIndex slot);
bool InputGuideArbiter_ShouldSlotDispatchDryRunConsume(PlayerInputSlotIndex slot);
PlayerInputSlotIndex InputGuideArbiter_GetSingleLiveConsumeSlotIndex();
const LogicalInputState* InputGuideArbiter_GetSlotStagedLogicalForDispatch(PlayerInputSlotIndex slot);
const VirtualInputConsumerFrame* InputGuideArbiter_TryGetSlotStagedMergedForDispatch(PlayerInputSlotIndex slot);
const PlayerSlotStagedActionSnapshot* InputGuideArbiter_TryGetSlotStagedActionForDispatch(PlayerInputSlotIndex slot);

void InputGuideArbiter_FormatSingleLiveConsumeSlotTagForT18(wchar_t* buf, size_t bufCount);
// Step15/16/21: default off. Live Apply on trial target only when armed + ManualOverride Live + keyboard-bound route.
void InputGuideArbiter_SetLiveConsumeTrialArmed(bool armed);
bool InputGuideArbiter_IsLiveConsumeTrialArmed();
PlayerInputSlotIndex InputGuideArbiter_GetLiveConsumeTrialTargetSlot();
void InputGuideArbiter_FormatLiveConsumeTrialTargetObsSuffixForT18(wchar_t* buf, size_t bufCount);
// F8/F9 apply to the trial target slot; if target is "none", slot1 (2P) is used (legacy demo).
PlayerInputSlotIndex InputGuideArbiter_GetLiveConsumeTrialHotkeySlot();
#if defined(_DEBUG)
// Cycles trial target 2P→3P→4P→none (Debug only). F7 is reserved for T17 scroll.
void InputGuideArbiter_DebugCycleLiveConsumeTrialTargetSlot();
#endif
// Legacy: true iff resolved live slot index is 1 (2P).
bool InputGuideArbiter_IsSlot1LiveConsumeTrialActive();
// True when a non-primary slot owns the single live consume stream (hold on 1P).
bool InputGuideArbiter_IsSlot0LiveConsumeHeldForSlot1KbTrial();
// Debug: call once per consume pass; logs only on change (spam-safe).
void InputGuideArbiter_DebugLogSlot1TrialObsIfChanged();

// Resolved policy (default seed or manual override).
PlayerSlotActualConsumePolicy InputGuideArbiter_GetSlotActualConsumePolicy(PlayerInputSlotIndex slot);
// Seat/binding seed only (ignores ManualOverride).
PlayerSlotActualConsumePolicy InputGuideArbiter_GetSlotDefaultStep13ConsumePolicy(PlayerInputSlotIndex slot);
PlayerSlotConsumePolicySource InputGuideArbiter_GetSlotConsumePolicySource(PlayerInputSlotIndex slot);
void InputGuideArbiter_SetSlotActualConsumePolicyOverride(
    PlayerInputSlotIndex slot,
    PlayerSlotActualConsumePolicy policy);
void InputGuideArbiter_ClearSlotActualConsumePolicyOverride(PlayerInputSlotIndex slot);
