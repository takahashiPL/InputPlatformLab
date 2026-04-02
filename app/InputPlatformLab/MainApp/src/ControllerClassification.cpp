#include "ControllerClassification.h"

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
};
} // namespace

// テーブル一致時は Ds4KnownHid や verified を付与。無ければ GenericHid + tentative に落とす。
void Win32_ResolveHidProductTable(
    std::uint16_t vid,
    std::uint16_t pid,
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
    }

    // Nintendo (HID)
    if (t.vendor_id == 0x057E)
    {
        return GameControllerKind::Nintendo;
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
