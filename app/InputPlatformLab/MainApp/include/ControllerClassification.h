// T21: HID 属性・VID/PID テーブル・GameControllerKind 推定（Win32/Raw Input API なし）
#pragma once

#include "GamepadTypes.h"

// 入力の解釈経路（family とは別。Ds4KnownHid は既知レポート前提、GenericHid は汎用、XInput は API 経路）。
enum class ControllerParserKind : UINT8
{
    None = 0,
    XInput,
    Ds4KnownHid,
    GenericHid,
};

enum class ControllerSupportLevel : UINT8
{
    Verified = 0,
    Tentative,
};

// Raw Input の RIDI_DEVICEINFO 相当（分類用。取得は呼び出し側）
struct GameControllerHidSummary
{
    UINT16 vendor_id; // USB VID
    UINT16 product_id; // USB PID
    UINT16 usage_page;
    UINT16 usage;
    bool device_info_valid;
};

bool Win32_HidTraitsLookLikeGamepad(const GameControllerHidSummary& t);

// VID/PID → parser / support（DS4 のみ verified+Known HID。他はテーブル行があれば family のみ寄せ、未実機は決め打ちマッピングを増やさない暫定受け皿）
struct ControllerHidProductTableEntry
{
    UINT16 vid;
    UINT16 pid; // 0xFFFF = その VID の残り PID すべて（具体 PID 行より後に置く）
    GameControllerKind family;
    ControllerParserKind parser;
    ControllerSupportLevel support;
};

void Win32_ResolveHidProductTable(
    UINT16 vid,
    UINT16 pid,
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
