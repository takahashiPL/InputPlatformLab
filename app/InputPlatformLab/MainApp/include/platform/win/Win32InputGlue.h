// Pack-out: small Win32 input glue (Raw Input registration, XInput slot probe, device string read).
// Post-foundation step1: split from MainApp.cpp; not input/core.
#pragma once

#include <Windows.h>

// XInput slot probe result (moved from MainApp.cpp with probe helpers).
struct ControllerSlotProbeResult
{
    UINT8 slot; // 0..3
    bool connected;
    UINT8 type; // XINPUT_CAPABILITIES::Type
    UINT8 sub_type; // XINPUT_CAPABILITIES::SubType
    UINT16 flags; // XINPUT_CAPABILITIES::Flags
};

BOOL Win32InputGlue_RegisterKeyboardRawInput(HWND hwnd);
void Win32InputGlue_FillControllerSlotProbe(UINT8 slot, ControllerSlotProbeResult& out);
void Win32InputGlue_LogControllerSlotProbeLine(const ControllerSlotProbeResult& probe);
void Win32InputGlue_LogXInputSlotsAtStartup();
bool Win32InputGlue_QueryAnyXInputConnected();
bool Win32InputGlue_TryGetRawInputDeviceString(
    HANDLE hDevice,
    UINT infoType,
    wchar_t* buffer,
    size_t bufferCount);
