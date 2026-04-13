// Pack-out: small Win32 input glue (Raw Input registration, device list fetch, first connected XInput slot,
// XInput slot probe, device string read, Raw HID WM_INPUT survey helpers, inventory refresh throttle).
// Post-foundation step1–3: split from MainApp.cpp; not input/core.
#pragma once

#include <Windows.h>

#include "ControllerClassification.h"

#include <vector>

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

// First connected XInput user index, or XUSER_MAX_COUNT if none (XInputGetState probe).
DWORD Win32InputGlue_GetFirstConnectedXInputSlotOrMax();

// Two-step GetRawInputDeviceList; out cleared on failure. Ok + empty == zero devices.
enum class Win32InputGlue_RawInputDeviceListStatus
{
    Ok,
    CountQueryFailed,
    DeviceListFailed,
};
Win32InputGlue_RawInputDeviceListStatus Win32InputGlue_FetchRawInputDeviceList(
    std::vector<RAWINPUTDEVICELIST>& out);

// RIDI_DEVICEINFO from RAWINPUT (HID path). false if not HID or API failure.
bool Win32InputGlue_FillHidSummaryFromRawInput(const RAWINPUT* raw, GameControllerHidSummary& out);

// T76: Raw HID traffic-driven T18 inventory refresh throttle (400 ms, GetTickCount).
bool Win32InputGlue_ConsumeT76RawHidInventoryRefreshThrottle400ms();

// XInput user 0..3 connected flags (XInputGetCapabilities probe; same as slot probe).
void Win32InputGlue_FillXInputUserConnectedSlots4(bool outSlots[4]);
