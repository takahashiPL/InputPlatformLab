// T20: platform-neutral gamepad enums (shared by VirtualInput layer and Win32 backend)
#pragma once

#include <cstdint>

enum class GameControllerKind : std::uint8_t
{
    Unknown = 0,
    Xbox,
    PlayStation4,
    PlayStation5,
    Nintendo,
    XInputCompatible,
};

// 論理ボタン（物理番号・API とは未対応。将来 input/ 配下へ移設可能）
enum class GamepadButtonId : std::uint8_t
{
    South = 0,
    East,
    West,
    North,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
    Start,
    Select,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count,
};

// 左スティックの粗い方向（T13。将来 input/ 配下へ移設可能）
enum class GamepadLeftStickDir : std::uint8_t
{
    None = 0,
    Left,
    Right,
    Up,
    Down,
};
