// T18 app/debug glue (post-foundation step5). Behavior matches prior MainApp.cpp.

#include "framework.h"
#include "T18InventorySnapshotGlue.h"

#include <initguid.h>
#include <hidclass.h>
#include <setupapi.h>
#pragma comment(lib, "SetupAPI.lib")

#include <cstdio>
#include <vector>

// =============================================================================
// snapshot completion / classify path
// =============================================================================

namespace
{

bool RawInputProductLooksLikeDevicePath(const wchar_t* s)
{
    if (!s || s[0] == L'\0')
    {
        return false;
    }
    if (s[0] == L'\\')
    {
        return true;
    }
    if (wcsstr(s, L"HID#") != nullptr)
    {
        return true;
    }
    if (wcsstr(s, L"VID_") != nullptr && wcsstr(s, L"PID_") != nullptr)
    {
        return true;
    }
    return false;
}

bool HidDevicePathsEqualEnough(const wchar_t* a, const wchar_t* b)
{
    if (!a || !b || a[0] == L'\0' || b[0] == L'\0')
    {
        return false;
    }
    if (_wcsicmp(a, b) == 0)
    {
        return true;
    }
    const wchar_t* ca = wcsstr(a, L"HID#");
    const wchar_t* cb = wcsstr(b, L"HID#");
    if (ca && cb && _wcsicmp(ca, cb) == 0)
    {
        return true;
    }
    return false;
}

bool TrySetupDiDeviceDescriptionFromHidPath(const wchar_t* devicePath, wchar_t* out, size_t outCount)
{
    if (!devicePath || devicePath[0] == L'\0' || outCount == 0)
    {
        return false;
    }
    out[0] = L'\0';

    HDEVINFO hDevInfo =
        SetupDiGetClassDevs(&GUID_DEVINTERFACE_HID, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool found = false;
    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &GUID_DEVINTERFACE_HID, index, &ifData);
         ++index)
    {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA))
        {
            continue;
        }
        std::vector<BYTE> detailBuffer(required);
        auto* pDetail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(detailBuffer.data());
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        SP_DEVINFO_DATA devInfoData{};
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, pDetail, required, nullptr, &devInfoData))
        {
            continue;
        }
        if (!HidDevicePathsEqualEnough(devicePath, pDetail->DevicePath))
        {
            continue;
        }
        DWORD regType = 0;
        DWORD need = static_cast<DWORD>((outCount - 1) * sizeof(wchar_t));
        if (SetupDiGetDeviceRegistryPropertyW(
                hDevInfo,
                &devInfoData,
                SPDRP_DEVICEDESC,
                &regType,
                reinterpret_cast<PBYTE>(out),
                need,
                nullptr))
        {
            out[outCount - 1] = L'\0';
            if (out[0] != L'\0')
            {
                found = true;
            }
        }
        break;
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return found;
}

} // namespace

void T18Inventory_CompleteSnapshotFromSurvey(
    T18ControllerIdentifySnapshot& snap,
    const Win32InputGlue_T18InventorySurvey& inv)
{
    snap.xinput_slot = inv.first_connected_xinput_slot;
    const bool anyXInput = inv.any_xinput_connected;

    if (inv.hid_row_found)
    {
        const bool productUsable =
            (inv.hid_product_name_raw[0] != L'\0') && !RawInputProductLooksLikeDevicePath(inv.hid_product_name_raw);

        wchar_t setupDiProduct[256] = {};
        bool setupDiOk = false;
        if (inv.hid_device_path[0] != L'\0')
        {
            setupDiOk = TrySetupDiDeviceDescriptionFromHidPath(
                inv.hid_device_path, setupDiProduct, sizeof(setupDiProduct) / sizeof(wchar_t));
        }

        const wchar_t* const tableFallback =
            Win32_ControllerHidProductDisplayNameFallback(inv.hid_traits.vendor_id, inv.hid_traits.product_id);

        const wchar_t* classifyName = nullptr;
        if (productUsable)
        {
            classifyName = inv.hid_product_name_raw;
        }
        else if (setupDiOk && setupDiProduct[0] != L'\0')
        {
            classifyName = setupDiProduct;
        }
        else if (tableFallback != nullptr)
        {
            classifyName = tableFallback;
        }

        const wchar_t* pathPtr = (inv.hid_device_path[0] != L'\0') ? inv.hid_device_path : nullptr;

        snap.hid = inv.hid_traits;
        snap.hid_found = true;
        wcscpy_s(snap.device_path, inv.hid_device_path);
        if (productUsable)
        {
            wcscpy_s(snap.product_name, inv.hid_product_name_raw);
        }
        else if (setupDiOk && setupDiProduct[0] != L'\0')
        {
            wcscpy_s(snap.product_name, setupDiProduct);
        }
        else if (tableFallback != nullptr)
        {
            wcscpy_s(snap.product_name, tableFallback);
        }

        snap.inferred_kind =
            Win32_ClassifyGameControllerKind(inv.hid_traits, classifyName, pathPtr, anyXInput);
        Win32_ResolveHidProductTable(
            inv.hid_traits.vendor_id,
            inv.hid_traits.product_id,
            snap.parser_kind,
            snap.support_level);
    }

    if (!snap.hid_found)
    {
        snap.inferred_kind = anyXInput ? GameControllerKind::XInputCompatible : GameControllerKind::Unknown;
        if (snap.xinput_slot >= 0 || anyXInput)
        {
            snap.parser_kind = ControllerParserKind::XInput;
            snap.support_level = ControllerSupportLevel::Verified;
        }
    }

    T18Inventory_FillIdentifyRationale(snap, snap.rationale, sizeof(snap.rationale) / sizeof(wchar_t));
}

// =============================================================================
// rationale / debug output
// =============================================================================

void T18Inventory_FillIdentifyRationale(const T18ControllerIdentifySnapshot& s, wchar_t* buf, size_t bufCount)
{
    if (bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    if (!s.hid_found)
    {
        if (s.xinput_slot >= 0)
        {
            swprintf_s(
                buf,
                bufCount,
                L"No Raw HID gamepad in enum order; XInput slot %d → family/parser via XInput API (verified).",
                s.xinput_slot);
        }
        else
        {
            wcscpy_s(buf, bufCount, L"No HID match; no XInput slot.");
        }
        return;
    }
    if (s.support_level == ControllerSupportLevel::Verified && s.parser_kind == ControllerParserKind::Ds4KnownHid)
    {
        wcscpy_s(buf, bufCount, L"VID/PID in verified DS4 HID table; known report layout.");
        return;
    }
    if (s.support_level == ControllerSupportLevel::Verified && s.parser_kind == ControllerParserKind::XInput)
    {
        wcscpy_s(buf, bufCount, L"XInput-only path (no HID gamepad in Raw Input order); verified API input.");
        return;
    }
    const bool hori006d = (s.hid.vendor_id == 0x0F0D && s.hid.product_id == 0x006D);
    if (hori006d)
    {
        swprintf_s(
            buf,
            bufCount,
            L"HORI-class 0x0F0D/0x006D: 多くの構成でゲーム入力は XInput 側。HID は補助/二重デバイスになり得る。"
            L" parser=%s; support=%s（HID レポートマップは本ビルドでは未検証）。",
            Win32_ControllerParserKindLabel(s.parser_kind),
            Win32_ControllerSupportLevelLabel(s.support_level));
        return;
    }
    if (s.hid.vendor_id == 0x054C && s.support_level == ControllerSupportLevel::Tentative &&
        s.parser_kind == ControllerParserKind::GenericHid)
    {
        if (s.hid.product_id == 0x0CE6 || s.hid.product_id == 0x0DF2)
        {
            swprintf_s(
                buf,
                bufCount,
          L"family=%s: DualSense 系 PID（テーブル）。parser=%s は汎用 HID; レポート検証は未実施のため tentative。",
                Win32_GameControllerKindFamilyLabel(s.inferred_kind),
                Win32_ControllerParserKindLabel(s.parser_kind));
            return;
        }
        if (s.inferred_kind == GameControllerKind::PlayStation5)
        {
            swprintf_s(
                buf,
                bufCount,
                L"family=%s: 名称/用途から PS5 寄せ。parser=%s; support=%s（HID レポートは本ビルドでは未検証）。",
                Win32_GameControllerKindFamilyLabel(s.inferred_kind),
                Win32_ControllerParserKindLabel(s.parser_kind),
                Win32_ControllerSupportLevelLabel(s.support_level));
            return;
        }
        swprintf_s(
            buf,
            bufCount,
            L"family=%s: Sony PID=0x%04X。parser=%s; support=%s。"
            L" DS4 verified は 0x05C4/0x09CC のみ（既知レポート）。それ以外は GenericHid のまま。",
            Win32_GameControllerKindFamilyLabel(s.inferred_kind),
            static_cast<unsigned>(s.hid.product_id),
            Win32_ControllerParserKindLabel(s.parser_kind),
            Win32_ControllerSupportLevelLabel(s.support_level));
        return;
    }
    swprintf_s(
        buf,
        bufCount,
        L"family=%s: classify(VID/PID/name/path, XInput any). "
        L"parser=%s: generic HID path (no per-device verified report map in this build). "
        L"support=%s.",
        Win32_GameControllerKindFamilyLabel(s.inferred_kind),
        Win32_ControllerParserKindLabel(s.parser_kind),
        Win32_ControllerSupportLevelLabel(s.support_level));
}

void T18Inventory_OutputSnapshotDebugLines(const T18ControllerIdentifySnapshot& snap)
{
    const unsigned vid = snap.hid_found ? static_cast<unsigned>(snap.hid.vendor_id) : 0u;
    const unsigned pid = snap.hid_found ? static_cast<unsigned>(snap.hid.product_id) : 0u;
    const wchar_t* const pathHint =
        (snap.device_path[0] != L'\0') ? L"(see device_path(full))" : L"";
    wchar_t line[2048] = {};
    swprintf_s(
        line,
        sizeof(line) / sizeof(wchar_t),
        L"[T18] hid_found=%d slot=%d vid=0x%04X pid=0x%04X family=%s parser=%s support=%s product=\"%s\" path=\"%s\"\r\n",
        snap.hid_found ? 1 : 0,
        snap.xinput_slot,
        vid,
        pid,
        Win32_GameControllerKindFamilyLabel(snap.inferred_kind),
        Win32_ControllerParserKindLabel(snap.parser_kind),
        Win32_ControllerSupportLevelLabel(snap.support_level),
        (snap.product_name[0] != L'\0') ? snap.product_name : L"",
        pathHint);
    OutputDebugStringW(line);
    wchar_t rat[640] = {};
    swprintf_s(rat, sizeof(rat) / sizeof(wchar_t), L"[T18] rationale: %s\r\n", snap.rationale);
    OutputDebugStringW(rat);
}

// =============================================================================
// HUD presentation helpers
// =============================================================================

void T18Inventory_TruncateWideForPaint(const wchar_t* src, wchar_t* dst, size_t dstCount, size_t maxLen)
{
    if (!src || src[0] == L'\0')
    {
        wcscpy_s(dst, dstCount, L"(none)");
        return;
    }
    const size_t n = wcslen(src);
    if (n <= maxLen)
    {
        wcscpy_s(dst, dstCount, src);
        return;
    }
    wcsncpy_s(dst, dstCount, src, maxLen);
    wcscat_s(dst, dstCount, L"...");
}

void T18Inventory_FillWhyHudShort(const T18ControllerIdentifySnapshot& s, wchar_t* buf, size_t bufCount)
{
    if (bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    if (!s.hid_found)
    {
        if (s.xinput_slot >= 0)
        {
            swprintf_s(
                buf,
                bufCount,
                L"XInput-only (no HID in enum order)\r\n"
                L"verified API\r\n"
                L"details → [T18] rationale");
        }
        else
        {
            wcscpy_s(buf, bufCount, L"No HID; no XInput slot.");
        }
        return;
    }
    if (s.support_level == ControllerSupportLevel::Verified && s.parser_kind == ControllerParserKind::Ds4KnownHid)
    {
        wcscpy_s(
            buf,
            bufCount,
            L"DS4 verified table\r\n"
            L"known HID report map");
        return;
    }
    if (s.support_level == ControllerSupportLevel::Verified && s.parser_kind == ControllerParserKind::XInput)
    {
        wcscpy_s(
            buf,
            bufCount,
            L"XInput-only path\r\n"
            L"verified API");
        return;
    }
    const bool hori006d = (s.hid.vendor_id == 0x0F0D && s.hid.product_id == 0x006D);
    if (hori006d)
    {
        swprintf_s(
            buf,
            bufCount,
            L"%s · %s\r\n"
            L"HORI-class · XInput優先\r\n"
            L"details → [T18] rationale",
            Win32_ControllerSupportLevelLabel(s.support_level),
            Win32_ControllerParserKindLabel(s.parser_kind));
        return;
    }
    if (s.hid.vendor_id == 0x054C && s.support_level == ControllerSupportLevel::Tentative &&
        s.parser_kind == ControllerParserKind::GenericHid)
    {
        if (s.hid.product_id == 0x0CE6 || s.hid.product_id == 0x0DF2)
        {
            wcscpy_s(
                buf,
                bufCount,
                L"tentative · GenericHid\r\n"
                L"DualSense-class (USB)\r\n"
                L"details → [T18] rationale");
            return;
        }
        if (s.inferred_kind == GameControllerKind::PlayStation5)
        {
            wcscpy_s(
                buf,
                bufCount,
                L"tentative · GenericHid\r\n"
                L"Sony PS5-class (HID)\r\n"
                L"details → [T18] rationale");
            return;
        }
        wcscpy_s(
            buf,
            bufCount,
            L"tentative · GenericHid\r\n"
            L"Sony PS4-class (HID)\r\n"
            L"details → [T18] rationale");
        return;
    }
    swprintf_s(
        buf,
        bufCount,
        L"%s · %s\r\n"
        L"%s\r\n"
        L"details → [T18] rationale",
        Win32_ControllerSupportLevelLabel(s.support_level),
        Win32_ControllerParserKindLabel(s.parser_kind),
        L"no per-device HID map");
}

void T18Inventory_FillWhyHudSingleLine(const T18ControllerIdentifySnapshot& s, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    wchar_t tmp[384] = {};
    T18Inventory_FillWhyHudShort(s, tmp, sizeof(tmp) / sizeof(wchar_t));
    for (wchar_t* p = tmp; *p != L'\0'; ++p)
    {
        if (*p == L'\r' || *p == L'\n')
        {
            *p = L'·';
        }
    }
    T18Inventory_TruncateWideForPaint(tmp, buf, bufCount, 120);
    if (buf[0] == L'\0')
    {
        wcscpy_s(buf, bufCount, L"(none)");
    }
}
