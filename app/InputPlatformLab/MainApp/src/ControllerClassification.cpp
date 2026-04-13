#include "ControllerClassification.h"

#include <Windows.h>

#include <cstdio>
#include <cwchar>

// ---------------------------------------------------------------------------
// VID/PID テーブル・usage と製品名から family / parser / support を決める（Win32 API 呼び出しなし）
// ---------------------------------------------------------------------------

// usage page / usage または既知ベンダでゲームパッド寄りとみなす粗い判定。
bool Win32_HidTraitsLookLikeGamepad(const GameControllerHidSummary& t)
{
    return (t.usage_page == 0x01 && (t.usage == 0x04 || t.usage == 0x05)) ||
        (t.vendor_id == 0x045E || t.vendor_id == 0x054C || t.vendor_id == 0x057E);
}

namespace
{
// 具体 PID 行を先に、0xFFFF は「その VID の残り PID すべて」（ワイルドカード）
const ControllerHidProductTableEntry kControllerHidProductTable[] = {
    { 0x054C, 0x05C4, GameControllerKind::PlayStation4, ControllerParserKind::Ds4KnownHid,
        ControllerSupportLevel::Verified },
    { 0x054C, 0x09CC, GameControllerKind::PlayStation4, ControllerParserKind::Ds4KnownHid,
        ControllerSupportLevel::Verified },
    { 0x054C, 0x0CE6, GameControllerKind::PlayStation5, ControllerParserKind::GenericHid,
        ControllerSupportLevel::Tentative },
    { 0x054C, 0x0DF2, GameControllerKind::PlayStation5, ControllerParserKind::GenericHid,
        ControllerSupportLevel::Tentative },
    { 0x057E, 0xFFFF, GameControllerKind::Nintendo, ControllerParserKind::GenericHid,
        ControllerSupportLevel::Tentative },
    { 0x045E, 0xFFFF, GameControllerKind::Xbox, ControllerParserKind::GenericHid,
        ControllerSupportLevel::Tentative },
    // HORI: 代表的 XInput 互換パッド（実機ごとに PID 差あり）。HID は補助デバイス; レポート検証は未実施のため tentative のまま。
    { 0x0F0D, 0x006D, GameControllerKind::XInputCompatible, ControllerParserKind::GenericHid,
        ControllerSupportLevel::Tentative },
};
} // namespace

// テーブル一致時は Ds4KnownHid や verified を付与。無ければ GenericHid + tentative に落とす。
void Win32_ResolveHidProductTable(
    UINT16 vid,
    UINT16 pid,
    ControllerParserKind& outParser,
    ControllerSupportLevel& outSupport)
{
    for (const ControllerHidProductTableEntry& e : kControllerHidProductTable)
    {
        if (e.vid != vid)
        {
            continue;
        }
        if (e.pid == pid || e.pid == 0xFFFF)
        {
            outParser = e.parser;
            outSupport = e.support;
            return;
        }
    }
    outParser = ControllerParserKind::GenericHid;
    outSupport = ControllerSupportLevel::Tentative;
}

const wchar_t* Win32_ControllerHidProductDisplayNameFallback(UINT16 vid, UINT16 pid)
{
    if (vid == 0x0F0D && pid == 0x006D)
    {
        return L"HORI pad (0x0F0D/0x006D, tabulated)";
    }
    if (vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC))
    {
        return L"DualShock 4 (USB, DS4 table)";
    }
    if (vid == 0x054C && (pid == 0x0CE6 || pid == 0x0DF2))
    {
        return L"DualSense-class (USB, tabulated tentative)";
    }
    if (vid == 0x054C)
    {
        return L"Sony PS family HID (PID not in table)";
    }
    return nullptr;
}

// ログ・UI 用の短いラベル（enum の表示名）。
const wchar_t* Win32_ControllerParserKindLabel(ControllerParserKind p)
{
    switch (p)
    {
    case ControllerParserKind::None: return L"None";
    case ControllerParserKind::XInput: return L"XInput";
    case ControllerParserKind::Ds4KnownHid: return L"Ds4KnownHid";
    case ControllerParserKind::GenericHid: return L"GenericHid";
    default: return L"?";
    }
}

const wchar_t* Win32_ControllerSupportLevelLabel(ControllerSupportLevel s)
{
    switch (s)
    {
    case ControllerSupportLevel::Verified: return L"verified";
    case ControllerSupportLevel::Tentative: return L"tentative";
    default: return L"?";
    }
}

const wchar_t* Win32_GameControllerKindFamilyLabel(GameControllerKind kind)
{
    switch (kind)
    {
    case GameControllerKind::Xbox: return L"Xbox";
    case GameControllerKind::PlayStation4:
    case GameControllerKind::PlayStation5: return L"PlayStation";
    case GameControllerKind::Nintendo: return L"Nintendo";
    case GameControllerKind::XInputCompatible: return L"XInputCompatible";
    case GameControllerKind::Unknown:
    default: return L"Unknown";
    }
}

const wchar_t* Win32_GameControllerKindShortLabel(GameControllerKind kind)
{
    switch (kind)
    {
    case GameControllerKind::Xbox: return L"Xbox";
    case GameControllerKind::PlayStation4: return L"PS4";
    case GameControllerKind::PlayStation5: return L"PS5";
    case GameControllerKind::Nintendo: return L"Nintendo";
    case GameControllerKind::XInputCompatible: return L"XInputCompatible";
    default: return L"Unknown";
    }
}

const wchar_t* Win32_GamepadButtonIdName(GamepadButtonId id)
{
    switch (id)
    {
    case GamepadButtonId::South: return L"South";
    case GamepadButtonId::East: return L"East";
    case GamepadButtonId::West: return L"West";
    case GamepadButtonId::North: return L"North";
    case GamepadButtonId::L1: return L"L1";
    case GamepadButtonId::R1: return L"R1";
    case GamepadButtonId::L2: return L"L2";
    case GamepadButtonId::R2: return L"R2";
    case GamepadButtonId::L3: return L"L3";
    case GamepadButtonId::R3: return L"R3";
    case GamepadButtonId::Start: return L"Start";
    case GamepadButtonId::Select: return L"Select";
    case GamepadButtonId::DPadUp: return L"DPadUp";
    case GamepadButtonId::DPadDown: return L"DPadDown";
    case GamepadButtonId::DPadLeft: return L"DPadLeft";
    case GamepadButtonId::DPadRight: return L"DPadRight";
    default: return L"?";
    }
}

const wchar_t* Win32_GamepadButtonDisplayLabel(GamepadButtonId id, GameControllerKind family)
{
    const bool xboxLike =
        (family == GameControllerKind::Xbox || family == GameControllerKind::XInputCompatible);
    const bool ps = (family == GameControllerKind::PlayStation4 || family == GameControllerKind::PlayStation5);
    const bool unknownFamily =
        (family == GameControllerKind::Unknown || family == GameControllerKind::Nintendo);

    switch (id)
    {
    case GamepadButtonId::South:
        if (xboxLike) return L"A";
        if (ps) return L"\u00D7";
        return L"South";
    case GamepadButtonId::East:
        if (xboxLike) return L"B";
        if (ps) return L"\u25CB";
        return L"East";
    case GamepadButtonId::West:
        if (xboxLike) return L"X";
        if (ps) return L"\u25A1";
        return L"West";
    case GamepadButtonId::North:
        if (xboxLike) return L"Y";
        if (ps) return L"\u25B3";
        return L"North";
    case GamepadButtonId::L1: return L"L1";
    case GamepadButtonId::R1: return L"R1";
    case GamepadButtonId::L2: return L"L2";
    case GamepadButtonId::R2: return L"R2";
    case GamepadButtonId::L3: return L"L3";
    case GamepadButtonId::R3: return L"R3";
    case GamepadButtonId::Start:
        if (ps) return L"Options";
        if (unknownFamily) return L"Start";
        return L"Start";
    case GamepadButtonId::Select:
        if (ps) return L"Share";
        if (xboxLike) return L"Back";
        if (unknownFamily) return L"Select";
        return L"View";
    case GamepadButtonId::DPadUp: return L"DPadUp";
    case GamepadButtonId::DPadDown: return L"DPadDown";
    case GamepadButtonId::DPadLeft: return L"DPadLeft";
    case GamepadButtonId::DPadRight: return L"DPadRight";
    default: return L"?";
    }
}

void Win32_GamepadButton_LogLabelTablesAtStartup()
{
    OutputDebugStringW(L"--- Gamepad button labels (T10) ---\r\n");

    static const GameControllerKind kFamilies[] = {
        GameControllerKind::Xbox,
        GameControllerKind::PlayStation4,
        GameControllerKind::PlayStation5,
        GameControllerKind::Nintendo,
        GameControllerKind::XInputCompatible,
        GameControllerKind::Unknown,
    };

    for (GameControllerKind family : kFamilies)
    {
        wchar_t header[96] = {};
        swprintf_s(header, sizeof(header) / sizeof(header[0]), L"family=%s\r\n", Win32_GameControllerKindShortLabel(family));
        OutputDebugStringW(header);

        const auto count = static_cast<UINT8>(GamepadButtonId::Count);
        for (UINT8 i = 0; i < count; ++i)
        {
            const GamepadButtonId bid = static_cast<GamepadButtonId>(i);
            wchar_t line[192] = {};
            swprintf_s(
                line,
                sizeof(line) / sizeof(line[0]),
                L"  id=%s label=\"%s\"\r\n",
                Win32_GamepadButtonIdName(bid),
                Win32_GamepadButtonDisplayLabel(bid, family));
            OutputDebugStringW(line);
        }
    }
}

// HID と（任意で）製品名・パス文字列から family を推定。XInput 接続のみのときは XInputCompatible になり得る。
GameControllerKind Win32_ClassifyGameControllerKind(
    const GameControllerHidSummary& t,
    const wchar_t* productName,
    const wchar_t* devicePath,
    bool anyXInputConnected)
{
    if (!t.device_info_valid)
    {
        return GameControllerKind::Unknown;
    }

    const bool nameHasXbox = (productName != nullptr && wcsstr(productName, L"Xbox") != nullptr)
        || (devicePath != nullptr && wcsstr(devicePath, L"Xbox") != nullptr);

    // 1–2: Sony（DualSense を DualShock より先に）
    if (t.vendor_id == 0x054C)
    {
        if (productName != nullptr && wcsstr(productName, L"DualSense") != nullptr)
        {
            return GameControllerKind::PlayStation5;
        }
        if (t.product_id == 0x0CE6 || t.product_id == 0x0DF2)
        {
            return GameControllerKind::PlayStation5;
        }
        if (productName != nullptr &&
            (wcsstr(productName, L"DualShock") != nullptr ||
             wcsstr(productName, L"Wireless Controller") != nullptr))
        {
            return GameControllerKind::PlayStation4;
        }
        if (t.product_id == 0x05C4 || t.product_id == 0x09CC)
        {
            return GameControllerKind::PlayStation4;
        }
        // 名称・PID が取れない Sony HID ゲームパッド: PS4 ファミリに寄せる（verified ではない。parser はテーブルで決まる）。
        if (t.usage_page == 0x01 && (t.usage == 0x04 || t.usage == 0x05))
        {
            return GameControllerKind::PlayStation4;
        }
        return GameControllerKind::Unknown;
    }

    // Nintendo (HID)
    if (t.vendor_id == 0x057E)
    {
        return GameControllerKind::Nintendo;
    }

    // HORI 等 0x0F0D: XInput が接続していればゲーム入力は API 側で取れる。HID レポートは本アプリでは未検証のまま。
    if (t.vendor_id == 0x0F0D && anyXInputConnected)
    {
        return GameControllerKind::XInputCompatible;
    }

    // 3: Microsoft VID または Xbox 名称
    if (t.vendor_id == 0x045E || nameHasXbox)
    {
        return GameControllerKind::Xbox;
    }

    // 4: 上記以外で XInput が生きているなら互換パッド
    if (anyXInputConnected)
    {
        return GameControllerKind::XInputCompatible;
    }

    return GameControllerKind::Unknown;
}
