// T20: VirtualInputSnapshot, policy, keyboard→consumer merge (no Win32 / XInput types)
#pragma once

#include "GamepadTypes.h"
#include "VirtualInputMenuSample.h"

#include <cstdint>

// === T25 [3] VirtualInputSnapshot + helpers（中立） — 将来: VirtualInputSnapshot.h / VirtualInputHelpers.cpp ===
// 1 フレーム分の仮想入力（Win32 型なし。将来 input/ 配下へ移設可能）
struct VirtualInputSnapshot
{
    bool connected;
    GameControllerKind family;

    bool south;
    bool east;
    bool west;
    bool north;
    bool l1;
    bool r1;
    bool l3;
    bool r3;
    bool start;
    bool select;
    bool psHome;
    bool dpadUp;
    bool dpadDown;
    bool dpadLeft;
    bool dpadRight;

    bool l2Pressed;
    bool r2Pressed;
    std::uint8_t leftTriggerRaw;
    std::uint8_t rightTriggerRaw;

    std::int16_t leftStickX;
    std::int16_t leftStickY;
    std::int16_t rightStickX;
    std::int16_t rightStickY;

    bool leftInDeadzone;
    GamepadLeftStickDir leftDir;
    bool rightInDeadzone;
    GamepadLeftStickDir rightDir;
};

void VirtualInput_ResetDisconnected(VirtualInputSnapshot& s);
bool VirtualInput_IsButtonDown(const VirtualInputSnapshot& s, GamepadButtonId id);
bool VirtualInput_WasButtonPressed(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    GamepadButtonId id);
bool VirtualInput_WasButtonReleased(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    GamepadButtonId id);
bool VirtualInput_IsL2Pressed(const VirtualInputSnapshot& s);
bool VirtualInput_IsR2Pressed(const VirtualInputSnapshot& s);
GamepadLeftStickDir VirtualInput_GetLeftDir(const VirtualInputSnapshot& s);
GamepadLeftStickDir VirtualInput_GetRightDir(const VirtualInputSnapshot& s);
bool VirtualInput_LeftInDeadzone(const VirtualInputSnapshot& s);
bool VirtualInput_RightInDeadzone(const VirtualInputSnapshot& s);

// === T25 [4] VirtualInputPolicy（中立） — 将来: VirtualInputPolicy.h ===
// T17/T19: 仮想入力の最小ポリシー（Win32 / XInput 非依存。VirtualInputSnapshot と既存 helper の上に載せる）
//
// 固定ルール:
// - Confirm = South の pressed（遷移 1 フレーム）
// - Cancel  = East の pressed
// - Menu    = Start の pressed
// - Move    = DPad 優先（1 つでも押されていれば DPad のみ）。なければ左スティック最小 4 方向
// - DPad    = 斜め合成あり（各軸 -1/0/+1 にクランプ）
// - Move は curr スナップショットから held として読む / メニュー系は prev→curr で pressed（遷移）として読む

// DPad または左スティックから得た -1/0/+1 の移動（そのフレームの held）。
struct VirtualInputPolicyHeld
{
    std::int8_t moveX;
    std::int8_t moveY;
};

// South / East / Start の pressed エッジ（confirm / cancel / menu）。
struct VirtualInputPolicyMenuEdges
{
    bool confirm;
    bool cancel;
    bool menu;
};

std::int8_t VirtualInputPolicy_ClampNeg1_0_1(int v);
void VirtualInputPolicy_FillMoveFromDpad(const VirtualInputSnapshot& s, std::int8_t& outX, std::int8_t& outY);
void VirtualInputPolicy_FillMoveFromLeftStick(const VirtualInputSnapshot& s, std::int8_t& outX, std::int8_t& outY);
VirtualInputPolicyHeld VirtualInputPolicy_MoveHeld(const VirtualInputSnapshot& s);
VirtualInputPolicyMenuEdges VirtualInputPolicy_MenuEdges(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr);

// T21: VirtualInputConsumerFrame 型は VirtualInputMenuSample.h。ここでは policy から組み立てのみ。
VirtualInputConsumerFrame VirtualInputConsumer_BuildFrame(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr);

// T12: キーボード最小アクション（配列非依存の安定キーのみ）。タイマー境界で prev→curr エッジを取る。
struct KeyboardActionState
{
    bool up;
    bool down;
    bool left;
    bool right;
    bool enter;
    bool backspace;
    bool tab;
    bool f6;
    bool f5;
    bool pageUp;
    bool pageDown;
    bool home;
    bool end;
    bool f7;
    bool f8;
};

VirtualInputConsumerFrame VirtualInputConsumer_BuildFrameFromKeyboardState(
    const KeyboardActionState& prevKs,
    const KeyboardActionState& currKs);
// キーとパッドの ConsumerFrame を統合（移動はパッド優先、ボタンは OR）。
VirtualInputConsumerFrame VirtualInputConsumer_MergeKeyboardController(
    const VirtualInputConsumerFrame& kb,
    const VirtualInputConsumerFrame& pad);
