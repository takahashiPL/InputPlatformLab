// Win32 / XInput / Raw Input helpers extracted from MainApp.cpp (post-foundation step1–4; behavior unchanged).

#include "Win32InputGlue.h"

#include <cstdio>

#include <xinput.h>
#pragma comment(lib, "Xinput.lib")

namespace
{
void Win32InputGlueInternal_TryFillHidDevicenameAndProductName(
    HANDLE hDevice,
    wchar_t* pathBuf,
    size_t pathCap,
    wchar_t* productBuf,
    size_t productCap)
{
    Win32InputGlue_TryGetRawInputDeviceString(hDevice, RIDI_DEVICENAME, pathBuf, pathCap);
    Win32InputGlue_TryGetRawInputDeviceString(hDevice, RIDI_PRODUCTNAME, productBuf, productCap);
}
}

BOOL Win32InputGlue_RegisterKeyboardRawInput(HWND hwnd)
{
    RAWINPUTDEVICE rids[2] = {};
    rids[0].usUsagePage = 0x01;
    rids[0].usUsage = 0x06; // keyboard
    rids[0].dwFlags = 0;
    rids[0].hwndTarget = hwnd;
    rids[1].usUsagePage = 0x01;
    rids[1].usUsage = 0x05; // game pad
    rids[1].dwFlags = 0;
    rids[1].hwndTarget = hwnd;
    return RegisterRawInputDevices(rids, 2, sizeof(RAWINPUTDEVICE));
}

void Win32InputGlue_FillControllerSlotProbe(UINT8 slot, ControllerSlotProbeResult& out)
{
    out.slot = slot;
    out.connected = false;
    out.type = 0;
    out.sub_type = 0;
    out.flags = 0;

    XINPUT_CAPABILITIES caps = {};
    const DWORD err = XInputGetCapabilities(slot, XINPUT_FLAG_GAMEPAD, &caps);
    if (err == ERROR_SUCCESS)
    {
        out.connected = true;
        out.type = caps.Type;
        out.sub_type = caps.SubType;
        out.flags = caps.Flags;
    }
}

void Win32InputGlue_LogControllerSlotProbeLine(const ControllerSlotProbeResult& probe)
{
    wchar_t line[256];
    if (probe.connected)
    {
        swprintf_s(
            line,
            sizeof(line) / sizeof(line[0]),
            L"XInput: slot=%u connected=yes type=0x%02X subtype=0x%02X flags=0x%04X\r\n",
            static_cast<unsigned int>(probe.slot),
            static_cast<unsigned int>(probe.type),
            static_cast<unsigned int>(probe.sub_type),
            static_cast<unsigned int>(probe.flags));
    }
    else
    {
        swprintf_s(
            line,
            sizeof(line) / sizeof(line[0]),
            L"XInput: slot=%u connected=no\r\n",
            static_cast<unsigned int>(probe.slot));
    }
    OutputDebugStringW(line);
}

void Win32InputGlue_LogXInputSlotsAtStartup()
{
    for (UINT8 i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        ControllerSlotProbeResult probe{};
        Win32InputGlue_FillControllerSlotProbe(i, probe);
        Win32InputGlue_LogControllerSlotProbeLine(probe);
    }
}

void Win32InputGlue_LogRawInputHidGameControllersClassified()
{
    OutputDebugStringW(L"--- HID gamepads (Raw Input + classify) ---\r\n");

    const bool anyXInput = Win32InputGlue_QueryAnyXInputConnected();

    std::vector<RAWINPUTDEVICELIST> devices;
    const auto listSt = Win32InputGlue_FetchRawInputDeviceList(devices);
    if (listSt == Win32InputGlue_RawInputDeviceListStatus::CountQueryFailed)
    {
        OutputDebugStringW(L"GetRawInputDeviceList(count) failed\r\n");
        return;
    }
    if (listSt == Win32InputGlue_RawInputDeviceListStatus::DeviceListFailed)
    {
        OutputDebugStringW(L"GetRawInputDeviceList(list) failed\r\n");
        return;
    }
    if (devices.empty())
    {
        OutputDebugStringW(L"(no Raw Input devices)\r\n");
        return;
    }

    for (UINT i = 0; i < static_cast<UINT>(devices.size()); ++i)
    {
        if (devices[i].dwType != RIM_TYPEHID)
        {
            continue;
        }

        const HANDLE hDevice = devices[i].hDevice;

        GameControllerHidSummary traits = {};
        if (!Win32InputGlue_TryFillHidSummaryFromRawInputHandle(hDevice, traits))
        {
            continue;
        }

        if (!Win32_HidTraitsLookLikeGamepad(traits))
        {
            continue;
        }

        wchar_t pathBuf[512] = {};
        wchar_t productBuf[256] = {};
        Win32InputGlueInternal_TryFillHidDevicenameAndProductName(
            hDevice,
            pathBuf,
            _countof(pathBuf),
            productBuf,
            _countof(productBuf));

        const wchar_t* productPtr = (productBuf[0] != L'\0') ? productBuf : nullptr;
        const wchar_t* pathPtr = (pathBuf[0] != L'\0') ? pathBuf : nullptr;

        const GameControllerKind kind = Win32_ClassifyGameControllerKind(traits, productPtr, pathPtr, anyXInput);
        ControllerParserKind pk{};
        ControllerSupportLevel sl{};
        Win32_ResolveHidProductTable(traits.vendor_id, traits.product_id, pk, sl);

        wchar_t line[1024] = {};
        swprintf_s(
            line,
            _countof(line),
            L"Gamepad: kind=%s vid=0x%04X pid=0x%04X usage=0x%04X/0x%04X xinput_any=%d name=\"%s\" path=\"%s\" "
            L"parser=%s support=%s\r\n",
            Win32_GameControllerKindShortLabel(kind),
            static_cast<unsigned int>(traits.vendor_id),
            static_cast<unsigned int>(traits.product_id),
            static_cast<unsigned int>(traits.usage_page),
            static_cast<unsigned int>(traits.usage),
            anyXInput ? 1 : 0,
            productPtr ? productPtr : L"",
            pathPtr ? pathPtr : L"",
            Win32_ControllerParserKindLabel(pk),
            Win32_ControllerSupportLevelLabel(sl));
        OutputDebugStringW(line);
    }
}

bool Win32InputGlue_QueryAnyXInputConnected()
{
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        XINPUT_CAPABILITIES caps = {};
        if (XInputGetCapabilities(i, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS)
        {
            return true;
        }
    }
    return false;
}

bool Win32InputGlue_TryGetRawInputDeviceString(
    HANDLE hDevice,
    UINT infoType,
    wchar_t* buffer,
    size_t bufferCount)
{
    if (bufferCount == 0)
    {
        return false;
    }
    buffer[0] = L'\0';
    UINT cbSize = 0;
    if (GetRawInputDeviceInfo(hDevice, infoType, nullptr, &cbSize) == static_cast<UINT>(-1))
    {
        return false;
    }
    if (cbSize < sizeof(wchar_t))
    {
        return false;
    }
    const size_t maxBytes = (bufferCount - 1) * sizeof(wchar_t);
    if (cbSize > maxBytes)
    {
        cbSize = static_cast<UINT>(maxBytes);
    }
    if (GetRawInputDeviceInfo(hDevice, infoType, buffer, &cbSize) == static_cast<UINT>(-1))
    {
        return false;
    }
    buffer[bufferCount - 1] = L'\0';
    return true;
}

DWORD Win32InputGlue_GetFirstConnectedXInputSlotOrMax()
{
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        XINPUT_STATE state = {};
        if (XInputGetState(i, &state) == ERROR_SUCCESS)
        {
            return i;
        }
    }
    return XUSER_MAX_COUNT;
}

Win32InputGlue_RawInputDeviceListStatus Win32InputGlue_FetchRawInputDeviceList(
    std::vector<RAWINPUTDEVICELIST>& out)
{
    out.clear();
    UINT numDevices = 0;
    if (GetRawInputDeviceList(nullptr, &numDevices, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
    {
        return Win32InputGlue_RawInputDeviceListStatus::CountQueryFailed;
    }
    if (numDevices == 0)
    {
        return Win32InputGlue_RawInputDeviceListStatus::Ok;
    }
    out.resize(numDevices);
    UINT copyCount = numDevices;
    if (GetRawInputDeviceList(out.data(), &copyCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
    {
        out.clear();
        return Win32InputGlue_RawInputDeviceListStatus::DeviceListFailed;
    }
    out.resize(copyCount);
    return Win32InputGlue_RawInputDeviceListStatus::Ok;
}

bool Win32InputGlue_TryFillHidSummaryFromRawInputHandle(HANDLE hDevice, GameControllerHidSummary& out)
{
    RID_DEVICE_INFO info = {};
    info.cbSize = sizeof(info);
    UINT cb = sizeof(info);
    if (GetRawInputDeviceInfo(hDevice, RIDI_DEVICEINFO, &info, &cb) == static_cast<UINT>(-1))
    {
        return false;
    }
    if (info.dwType != RIM_TYPEHID)
    {
        return false;
    }
    out = {};
    out.device_info_valid = true;
    out.vendor_id = static_cast<UINT16>(info.hid.dwVendorId);
    out.product_id = static_cast<UINT16>(info.hid.dwProductId);
    out.usage_page = info.hid.usUsagePage;
    out.usage = info.hid.usUsage;
    return true;
}

bool Win32InputGlue_FillHidSummaryFromRawInput(const RAWINPUT* raw, GameControllerHidSummary& out)
{
    if (raw == nullptr || raw->header.dwType != RIM_TYPEHID)
    {
        return false;
    }
    return Win32InputGlue_TryFillHidSummaryFromRawInputHandle(raw->header.hDevice, out);
}

bool Win32InputGlue_ConsumeT76RawHidInventoryRefreshThrottle400ms()
{
    static DWORD s_lastTick = 0;
    const DWORD now = GetTickCount();
    if (s_lastTick != 0 && (now - s_lastTick) < 400u)
    {
        return false;
    }
    s_lastTick = now;
    return true;
}

void Win32InputGlue_FillXInputUserConnectedSlots4(bool outSlots[4])
{
    static_assert(XUSER_MAX_COUNT == 4, "xinputUserConnected[4] must match XUSER_MAX_COUNT");
    for (UINT8 i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        ControllerSlotProbeResult probe{};
        Win32InputGlue_FillControllerSlotProbe(i, probe);
        outSlots[i] = probe.connected;
    }
}

void Win32InputGlue_SurveyForT18InventoryRefresh(Win32InputGlue_T18InventorySurvey& out)
{
    out = {};
    const DWORD slotDw = Win32InputGlue_GetFirstConnectedXInputSlotOrMax();
    if (slotDw < XUSER_MAX_COUNT)
    {
        out.first_connected_xinput_slot = static_cast<int>(slotDw);
    }
    else
    {
        out.first_connected_xinput_slot = -1;
    }
    out.any_xinput_connected = Win32InputGlue_QueryAnyXInputConnected();

    std::vector<RAWINPUTDEVICELIST> devices;
    const auto listSt = Win32InputGlue_FetchRawInputDeviceList(devices);
    if (listSt != Win32InputGlue_RawInputDeviceListStatus::Ok || devices.empty())
    {
        return;
    }

    for (UINT i = 0; i < static_cast<UINT>(devices.size()); ++i)
    {
        if (devices[i].dwType != RIM_TYPEHID)
        {
            continue;
        }

        const HANDLE hDevice = devices[i].hDevice;

        GameControllerHidSummary traits = {};
        if (!Win32InputGlue_TryFillHidSummaryFromRawInputHandle(hDevice, traits))
        {
            continue;
        }

        if (!Win32_HidTraitsLookLikeGamepad(traits))
        {
            continue;
        }

        Win32InputGlueInternal_TryFillHidDevicenameAndProductName(
            hDevice,
            out.hid_device_path,
            sizeof(out.hid_device_path) / sizeof(out.hid_device_path[0]),
            out.hid_product_name_raw,
            sizeof(out.hid_product_name_raw) / sizeof(out.hid_product_name_raw[0]));

        out.hid_traits = traits;
        out.hid_row_found = true;
        return;
    }
}
