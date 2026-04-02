// T21: HID 属性・VID/PID テーブル・GameControllerKind 推定（Win32/Raw Input API なし）
#pragma once

#include "GamepadTypes.h"

#include <cstdint>

// 入力経路: XInput / 既知 Raw HID(DS4) / 汎用 HID。実機検証度は SupportLevel。
enum class ControllerParserKind : std::uint8_t
{
    None = 0,
    XInput,
    Ds4KnownHid,
    GenericHid,
};

enum class ControllerSupportLevel : std::uint8_t
{
    Verified = 0,
    Tentative,
};

// Raw Input HID から得た属性（分類用。取得は Win32 側）
struct GameControllerHidSummary
{
    std::uint16_t vendor_id;
    std::uint16_t product_id;
    std::uint16_t usage_page;
    std::uint16_t usage;
    bool device_info_valid;
};

bool Win32_HidTraitsLookLikeGamepad(const GameControllerHidSummary& t);

// VID/PID → parser / support（DS4 のみ verified+Known HID。他はテーブル行があれば family のみ寄せ、未実機は決め打ちマッピングを増やさない暫定受け皿）
struct ControllerHidProductTableEntry
{
    std::uint16_t vid;
    std::uint16_t pid; // 0xFFFF = その VID の残り PID すべて（具体 PID 行より後に置く）
    GameControllerKind family;
    ControllerParserKind parser;
    ControllerSupportLevel support;
};

void Win32_ResolveHidProductTable(
    std::uint16_t vid,
    std::uint16_t pid,
    ControllerParserKind& outParser,
    ControllerSupportLevel& outSupport);

const wchar_t* Win32_ControllerParserKindLabel(ControllerParserKind p);
const wchar_t* Win32_ControllerSupportLevelLabel(ControllerSupportLevel s);

// HID + 製品名 / デバイスパス文字列（任意）から family を推定。文字列は wchar_t（CRT の wcsstr のみ使用）
GameControllerKind Win32_ClassifyGameControllerKind(
    const GameControllerHidSummary& traits,
    const wchar_t* productName,
    const wchar_t* devicePath,
    bool anyXInputConnected);
