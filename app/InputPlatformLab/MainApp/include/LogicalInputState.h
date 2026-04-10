// 論理ボタン単位のフレーム状態（キーボード + ゲームパッドを OR 合成後に 1 パスで更新）
//
// PS4 DS4 HID（verified マップ, MainApp Win32_FillVirtualInputFromDs4StyleHidReport）→ GamepadButtonId:
//   Cross→South, Circle→East, Square→West(b5&0x10), Triangle→North(b5&0x80), Share→Select(b6&0x10), Options→Start,
//   hat→DPadUp/Down/Left/Right。
//   L1(b6&0x01), R1(b6&0x02), L2 デジタル(b6&0x04) または L2 アナログ(b8)≥閾値, 同様に R2(b6&0x08)/b9,
//   L3(b6&0x40), R3(b6&0x80) → VirtualInput l1/r1/l2Pressed/r2Pressed/l3/r3 → 上記と同一の GamepadButtonId。
//   LogicalInput_FillCurrentDownFromSources は上記にキーボード
//   （Enter/Backspace/Tab/矢印）を OR。LogicalInputState_Update の press/release/push/hold は
//   全ソース同一式（WM_TIMER 1 tick = 論理 1 フレーム）。
//
// 「1 フレーム」の意味（本層）:
//   LogicalInputState_Update は MainApp の WM_TIMER（XINPUT_POLL）1 回につき 1 回だけ走る。
//   よって press / release / push / holdFrames の単位は「WM_TIMER tick 基準」であり、
//   Raw キー（WM_INPUT）や表示用の 1 フレームとは一致しない場合がある。
//
// LogicalInputState_Reset() を呼ぶ候補（必要になったら MainApp 等で接続。必須ではない）:
//   - WM_KILLFOCUS / WM_ACTIVATE（非アクティブ化）: フォーカス喪失後も s_keyboardActionState が
//     キーアップを取りこぼすと、論理 down が残る可能性がある場合の保険。
//   - ゲームパッド切断直後: Win32_XInputPoll 内で VirtualInput がリセットされる境界と揃えて
//     prevDown / prevHoldFrames を捨て、次タイマーでクリーンに再構築したいとき。
//   - アプリ終了前や「入力デバイス再列挙」後の明示クリアが必要な場合。
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

// T77 step9: map menu consumer frame to logical down[] for one timer tick (dry-run / staging; not full device map).
void LogicalInput_FillCurrentDownFromConsumerFrame(
    bool outDown[static_cast<size_t>(LogicalButtonId::Count)],
    const VirtualInputConsumerFrame& f);

inline const LogicalButtonFrameState& LogicalInputState_Frame(
    const LogicalInputState& st,
    LogicalButtonId id)
{
    return st.frames[static_cast<size_t>(id)];
}

// MainApp: WM_TIMER（1 tick = 上記の論理 1 フレーム）で XInput/仮想入力更新の直後、
// VirtualInputConsumer（メニュー）より前に 1 回更新。参照はその後いつでも可。
const LogicalInputState* InputCore_LogicalInputState();
