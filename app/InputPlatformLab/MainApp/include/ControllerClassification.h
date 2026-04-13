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
// 0x0F0D HORI 等は XInput 経路でゲーム入力が取れることが多いが、HID レポートの検証マップは本ビルドでは持たず GenericHid+tentative のまま。
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

// T18 表示用: Raw Input の製品文字列・SetupDi が無いときの短い人間可読名（VID/PID 既知行・Sony 既定）。nullptr なら (none) 扱い。
const wchar_t* Win32_ControllerHidProductDisplayNameFallback(UINT16 vid, UINT16 pid);

const wchar_t* Win32_ControllerParserKindLabel(ControllerParserKind p);
const wchar_t* Win32_ControllerSupportLevelLabel(ControllerSupportLevel s);

// T18 / paint: 粗い family（PS4/PS5 は PlayStation にまとめる。XInput 互換は enum 名どおり）
const wchar_t* Win32_GameControllerKindFamilyLabel(GameControllerKind kind);

// ログ・短い表示用（PS4 / PS5 を区別）。まとめ表示は Win32_GameControllerKindFamilyLabel。
const wchar_t* Win32_GameControllerKindShortLabel(GameControllerKind kind);

// T10/T25: 論理ボタン ID名・ファミリ別の表示ラベル（HUD/ログ用。入力の解釈経路とは無関係）
const wchar_t* Win32_GamepadButtonIdName(GamepadButtonId id);
const wchar_t* Win32_GamepadButtonDisplayLabel(GamepadButtonId id, GameControllerKind family);

// T13: 左スティックの粗い方向（HUD/ログ用）
const wchar_t* Win32_GamepadLeftStickDirLabel(GamepadLeftStickDir d);

// HID + 製品名 / デバイスパス文字列（任意）から family を推定。文字列は wchar_t（CRT の wcsstr のみ使用）
GameControllerKind Win32_ClassifyGameControllerKind(
    const GameControllerHidSummary& traits,
    const wchar_t* productName,
    const wchar_t* devicePath,
    bool anyXInputConnected);
