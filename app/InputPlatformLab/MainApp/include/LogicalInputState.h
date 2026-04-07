// 論理ボタン単位のフレーム状態（キーボード + ゲームパッドを OR 合成後に 1 パスで更新）
#pragma once

#include "CommonTypes.h"
#include "GamepadTypes.h"

#include "VirtualInputNeutral.h"

// GamepadButtonId と同一レイアウト（将来の分離用に別名）
enum class LogicalButtonId : UINT8
{
    South = static_cast<UINT8>(GamepadButtonId::South),
    East = static_cast<UINT8>(GamepadButtonId::East),
    West = static_cast<UINT8>(GamepadButtonId::West),
    North = static_cast<UINT8>(GamepadButtonId::North),
    L1 = static_cast<UINT8>(GamepadButtonId::L1),
    R1 = static_cast<UINT8>(GamepadButtonId::R1),
    L2 = static_cast<UINT8>(GamepadButtonId::L2),
    R2 = static_cast<UINT8>(GamepadButtonId::R2),
    L3 = static_cast<UINT8>(GamepadButtonId::L3),
    R3 = static_cast<UINT8>(GamepadButtonId::R3),
    Start = static_cast<UINT8>(GamepadButtonId::Start),
    Select = static_cast<UINT8>(GamepadButtonId::Select),
    DPadUp = static_cast<UINT8>(GamepadButtonId::DPadUp),
    DPadDown = static_cast<UINT8>(GamepadButtonId::DPadDown),
    DPadLeft = static_cast<UINT8>(GamepadButtonId::DPadLeft),
    DPadRight = static_cast<UINT8>(GamepadButtonId::DPadRight),
    Count = static_cast<UINT8>(GamepadButtonId::Count),
};

static_assert(
    static_cast<size_t>(LogicalButtonId::Count) == static_cast<size_t>(GamepadButtonId::Count),
    "LogicalButtonId layout must match GamepadButtonId");

struct LogicalButtonFrameState
{
    bool down;
    bool press;
    bool release;
    bool push;
    UINT8 holdFrames;
};

struct LogicalInputState
{
    LogicalButtonFrameState frames[static_cast<size_t>(LogicalButtonId::Count)];
    bool prevDown[static_cast<size_t>(LogicalButtonId::Count)];
    UINT8 prevHoldFrames[static_cast<size_t>(LogicalButtonId::Count)];
};

void LogicalInputState_Reset(LogicalInputState& st);

// currentDown[i] はそのフレームの合成 down（キーボード OR ゲームパッド）
void LogicalInputState_Update(
    LogicalInputState& st,
    const bool currentDown[static_cast<size_t>(LogicalButtonId::Count)]);

void LogicalInput_FillCurrentDownFromSources(
    bool outDown[static_cast<size_t>(LogicalButtonId::Count)],
    const KeyboardActionState& kb,
    const VirtualInputSnapshot& pad);

inline const LogicalButtonFrameState& LogicalInputState_Frame(
    const LogicalInputState& st,
    LogicalButtonId id)
{
    return st.frames[static_cast<size_t>(id)];
}

// MainApp: WM_TIMER で XInput/仮想入力更新の直後、VirtualInputConsumer（メニュー）より前に 1 回更新。参照はその後いつでも可。
const LogicalInputState* InputCore_LogicalInputState();
