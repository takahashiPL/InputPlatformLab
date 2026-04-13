// Win32 / XInput / Raw Input helpers extracted from MainApp.cpp (post-foundation step1; behavior unchanged).

#include "Win32InputGlue.h"

#include <cstdio>

#include <xinput.h>
#pragma comment(lib, "Xinput.lib")

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
