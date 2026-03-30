// MainApp.cpp : アプリケーションのエントリ ポイントを定義します。
//

#include "framework.h"
#include "MainApp.h"

#include <cstdint>
#include <climits>
#include <cmath>
#include <stdio.h>
#include <vector>

#include <xinput.h>
#pragma comment(lib, "Xinput.lib")

#define MAX_LOADSTRING 100

#define TIMER_ID_XINPUT_POLL 1001
#define XINPUT_POLL_INTERVAL_MS 33

#ifndef XINPUT_GAMEPAD_TRIGGER_THRESHOLD
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD 30
#endif

#ifndef XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7849
#endif

// Windows 8.1+（targetver によっては未定義のため）
#ifndef RIDI_PRODUCTNAME
#define RIDI_PRODUCTNAME 0x20000007
#endif

// プラットフォーム中立な物理キーイベント（将来 input/ 配下へ移設可能）
struct PhysicalKeyEvent
{
    std::uint16_t native_key_code; // Win32 では仮想キー（VK）に相当
    std::uint16_t scan_code;       // スキャンコード（拡張プレフィックスは is_extended_* と併用）
    bool is_extended_0;            // 拡張キー前置 E0 相当
    bool is_extended_1;            // 拡張キー前置 E1 相当
    bool is_key_up;                // 離上（break）なら true
};

// XInput スロット列挙結果（将来 input/ 配下へ移設可能）
struct ControllerSlotProbeResult
{
    std::uint8_t slot;        // 0..3
    bool connected;
    std::uint8_t type;        // XINPUT_CAPABILITIES::Type
    std::uint8_t sub_type;    // XINPUT_CAPABILITIES::SubType
    std::uint16_t flags;      // XINPUT_CAPABILITIES::Flags
};

enum class GameControllerKind : std::uint8_t
{
    Unknown = 0,
    Xbox,
    PlayStation4,
    PlayStation5,
    XInputCompatible,
};

// 論理ボタン（物理番号・API とは未対応。将来 input/ 配下へ移設可能）
enum class GamepadButtonId : std::uint8_t
{
    South = 0,
    East,
    West,
    North,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
    Start,
    Select,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count,
};

// 左スティックの粗い方向（T13。将来 input/ 配下へ移設可能）
enum class GamepadLeftStickDir : std::uint8_t
{
    None = 0,
    Left,
    Right,
    Up,
    Down,
};

// Raw Input HID から得た属性（将来 input/ 配下へ移設可能）
struct GameControllerHidSummary
{
    std::uint16_t vendor_id;
    std::uint16_t product_id;
    std::uint16_t usage_page;
    std::uint16_t usage;
    bool device_info_valid;
};

// グローバル変数:
HINSTANCE hInst;                                // 現在のインターフェイス
WCHAR szTitle[MAX_LOADSTRING];                  // タイトル バーのテキスト
WCHAR szWindowClass[MAX_LOADSTRING];            // メイン ウィンドウ クラス名

// このコード モジュールに含まれる関数の宣言を転送します:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

static BOOL Win32_RegisterKeyboardRawInput(HWND hwnd);
static bool Win32_TryFillPhysicalKeyFromRawInput(HRAWINPUT hRaw, PhysicalKeyEvent& out);
static bool Win32_TryFillDisplayLabel(const PhysicalKeyEvent& ev, wchar_t* buffer, size_t bufferCount);
static void Win32_FillLayoutTag(wchar_t* buffer, size_t bufferCount);
static void PhysicalKey_FormatDebugLine(const PhysicalKeyEvent& ev, const wchar_t* displayLabel, const wchar_t* layoutTag, wchar_t* buffer, size_t bufferCount);

static void Win32_FillControllerSlotProbe(std::uint8_t slot, ControllerSlotProbeResult& out);
static void Win32_LogControllerSlotProbeLine(const ControllerSlotProbeResult& probe);
static void Win32_LogXInputSlotsAtStartup();

static bool Win32_QueryAnyXInputConnected();
static GameControllerKind Win32_ClassifyGameControllerKind(
    const GameControllerHidSummary& traits,
    const wchar_t* productName,
    const wchar_t* devicePath,
    bool anyXInputConnected);
static const wchar_t* Win32_GameControllerKindLabel(GameControllerKind kind);
static bool Win32_TryGetRawInputDeviceString(HANDLE hDevice, UINT infoType, wchar_t* buffer, size_t bufferCount);
static void Win32_LogRawInputHidGameControllersClassified();

static const wchar_t* GamepadButton_GetIdName(GamepadButtonId id);
static const wchar_t* GamepadButton_GetDisplayLabel(GamepadButtonId id, GameControllerKind family);
static void GamepadButton_LogLabelTablesAtStartup();

static DWORD Win32_GetFirstConnectedXInputSlotOrMax();
static void Win32_XInputPollDigitalEdgesOnTimer(HWND hwnd);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: ここにコードを挿入してください。

    // グローバル文字列を初期化する
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MAINAPP, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // アプリケーション初期化の実行:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MAINAPP));

    MSG msg;

    // メイン メッセージ ループ:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  関数: MyRegisterClass()
//
//  目的: ウィンドウ クラスを登録します。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAINAPP));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_MAINAPP);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   関数: InitInstance(HINSTANCE, int)
//
//   目的: インスタンス ハンドルを保存して、メイン ウィンドウを作成します
//
//   コメント:
//
//        この関数で、グローバル変数でインスタンス ハンドルを保存し、
//        メイン プログラム ウィンドウを作成および表示します。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // グローバル変数にインスタンス ハンドルを格納する

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   if (!Win32_RegisterKeyboardRawInput(hWnd))
   {
      OutputDebugStringW(L"RegisterRawInputDevices failed\r\n");
   }

   Win32_LogXInputSlotsAtStartup();
   Win32_LogRawInputHidGameControllersClassified();
   GamepadButton_LogLabelTablesAtStartup();

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   SetTimer(hWnd, TIMER_ID_XINPUT_POLL, XINPUT_POLL_INTERVAL_MS, nullptr);

   return TRUE;
}

static BOOL Win32_RegisterKeyboardRawInput(HWND hwnd)
{
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x06;
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

static void Win32_FillControllerSlotProbe(std::uint8_t slot, ControllerSlotProbeResult& out)
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

static void Win32_LogControllerSlotProbeLine(const ControllerSlotProbeResult& probe)
{
    wchar_t line[256];
    if (probe.connected)
    {
        swprintf_s(line, _countof(line),
            L"XInput: slot=%u connected=yes type=0x%02X subtype=0x%02X flags=0x%04X\r\n",
            static_cast<unsigned int>(probe.slot),
            static_cast<unsigned int>(probe.type),
            static_cast<unsigned int>(probe.sub_type),
            static_cast<unsigned int>(probe.flags));
    }
    else
    {
        swprintf_s(line, _countof(line),
            L"XInput: slot=%u connected=no\r\n",
            static_cast<unsigned int>(probe.slot));
    }
    OutputDebugStringW(line);
}

static void Win32_LogXInputSlotsAtStartup()
{
    for (std::uint8_t i = 0; i < XUSER_MAX_COUNT; ++i)
    {
        ControllerSlotProbeResult probe{};
        Win32_FillControllerSlotProbe(i, probe);
        Win32_LogControllerSlotProbeLine(probe);
    }
}

static bool Win32_QueryAnyXInputConnected()
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

static const wchar_t* Win32_GameControllerKindLabel(GameControllerKind kind)
{
    switch (kind)
    {
    case GameControllerKind::Xbox: return L"Xbox";
    case GameControllerKind::PlayStation4: return L"PS4";
    case GameControllerKind::PlayStation5: return L"PS5";
    case GameControllerKind::XInputCompatible: return L"XInputCompatible";
    default: return L"Unknown";
    }
}

static const wchar_t* GamepadButton_GetIdName(GamepadButtonId id)
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

static const wchar_t* GamepadButton_GetDisplayLabel(GamepadButtonId id, GameControllerKind family)
{
    const bool xboxLike =
        (family == GameControllerKind::Xbox || family == GameControllerKind::XInputCompatible);
    const bool ps = (family == GameControllerKind::PlayStation4 || family == GameControllerKind::PlayStation5);
    const bool unknownFamily = (family == GameControllerKind::Unknown);

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

static void GamepadButton_LogLabelTablesAtStartup()
{
    OutputDebugStringW(L"--- Gamepad button labels (T10) ---\r\n");

    static const GameControllerKind kFamilies[] = {
        GameControllerKind::Xbox,
        GameControllerKind::PlayStation4,
        GameControllerKind::PlayStation5,
        GameControllerKind::XInputCompatible,
        GameControllerKind::Unknown,
    };

    for (GameControllerKind family : kFamilies)
    {
        wchar_t header[96] = {};
        swprintf_s(header, _countof(header), L"family=%s\r\n", Win32_GameControllerKindLabel(family));
        OutputDebugStringW(header);

        const auto count = static_cast<std::uint8_t>(GamepadButtonId::Count);
        for (std::uint8_t i = 0; i < count; ++i)
        {
            const GamepadButtonId bid = static_cast<GamepadButtonId>(i);
            wchar_t line[192] = {};
            swprintf_s(line, _countof(line), L"  id=%s label=\"%s\"\r\n",
                GamepadButton_GetIdName(bid),
                GamepadButton_GetDisplayLabel(bid, family));
            OutputDebugStringW(line);
        }
    }
}

struct XInputDigitalButtonMapEntry
{
    WORD mask;
    GamepadButtonId id;
};

static const XInputDigitalButtonMapEntry g_XInputDigitalMap[] = {
    { XINPUT_GAMEPAD_A, GamepadButtonId::South },
    { XINPUT_GAMEPAD_B, GamepadButtonId::East },
    { XINPUT_GAMEPAD_X, GamepadButtonId::West },
    { XINPUT_GAMEPAD_Y, GamepadButtonId::North },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButtonId::L1 },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButtonId::R1 },
    { XINPUT_GAMEPAD_LEFT_THUMB, GamepadButtonId::L3 },
    { XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButtonId::R3 },
    { XINPUT_GAMEPAD_START, GamepadButtonId::Start },
    { XINPUT_GAMEPAD_BACK, GamepadButtonId::Select },
    { XINPUT_GAMEPAD_DPAD_UP, GamepadButtonId::DPadUp },
    { XINPUT_GAMEPAD_DPAD_DOWN, GamepadButtonId::DPadDown },
    { XINPUT_GAMEPAD_DPAD_LEFT, GamepadButtonId::DPadLeft },
    { XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButtonId::DPadRight },
};

static DWORD s_xinputPollPrevSlot = XUSER_MAX_COUNT;
static WORD s_xinputPollPrevWButtons = 0;
static bool s_xinputPrevL2Pressed = false;
static bool s_xinputPrevR2Pressed = false;
static bool s_xinputPrevLeftInDeadzone = true;
static GamepadLeftStickDir s_xinputPrevLeftDir = GamepadLeftStickDir::None;

static bool Win32_LeftStickInDeadzone(SHORT x, SHORT y)
{
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double mag = std::sqrt(dx * dx + dy * dy);
    return mag < static_cast<double>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
}

static GamepadLeftStickDir Win32_ClassifyLeftStickDir(SHORT x, SHORT y, bool inDeadzone)
{
    if (inDeadzone)
    {
        return GamepadLeftStickDir::None;
    }
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    if (std::fabs(dx) >= std::fabs(dy))
    {
        return dx < 0.0 ? GamepadLeftStickDir::Left : GamepadLeftStickDir::Right;
    }
    return dy < 0.0 ? GamepadLeftStickDir::Down : GamepadLeftStickDir::Up;
}

static const wchar_t* Win32_LeftStickDirLabel(GamepadLeftStickDir d)
{
    switch (d)
    {
    case GamepadLeftStickDir::Left: return L"Left";
    case GamepadLeftStickDir::Right: return L"Right";
    case GamepadLeftStickDir::Up: return L"Up";
    case GamepadLeftStickDir::Down: return L"Down";
    default: return L"None";
    }
}

static DWORD Win32_GetFirstConnectedXInputSlotOrMax()
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

static void Win32_XInputPollDigitalEdgesOnTimer(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);

    constexpr GameControllerKind kFamily = GameControllerKind::Xbox;

    const DWORD slot = Win32_GetFirstConnectedXInputSlotOrMax();
    if (slot >= XUSER_MAX_COUNT)
    {
        s_xinputPollPrevSlot = XUSER_MAX_COUNT;
        s_xinputPollPrevWButtons = 0;
        s_xinputPrevL2Pressed = false;
        s_xinputPrevR2Pressed = false;
        s_xinputPrevLeftInDeadzone = true;
        s_xinputPrevLeftDir = GamepadLeftStickDir::None;
        return;
    }

    XINPUT_STATE state = {};
    if (XInputGetState(slot, &state) != ERROR_SUCCESS)
    {
        s_xinputPollPrevSlot = XUSER_MAX_COUNT;
        s_xinputPollPrevWButtons = 0;
        s_xinputPrevL2Pressed = false;
        s_xinputPrevR2Pressed = false;
        s_xinputPrevLeftInDeadzone = true;
        s_xinputPrevLeftDir = GamepadLeftStickDir::None;
        return;
    }

    const WORD w = state.Gamepad.wButtons;
    const BYTE lt = state.Gamepad.bLeftTrigger;
    const BYTE rt = state.Gamepad.bRightTrigger;
    const bool l2Now = (lt >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    const bool r2Now = (rt >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

    const SHORT lx = state.Gamepad.sThumbLX;
    const SHORT ly = state.Gamepad.sThumbLY;
    const bool leftInDz = Win32_LeftStickInDeadzone(lx, ly);
    const GamepadLeftStickDir leftDir = Win32_ClassifyLeftStickDir(lx, ly, leftInDz);

    if (slot != s_xinputPollPrevSlot)
    {
        s_xinputPollPrevSlot = slot;
        s_xinputPollPrevWButtons = w;
        s_xinputPrevL2Pressed = l2Now;
        s_xinputPrevR2Pressed = r2Now;
        s_xinputPrevLeftInDeadzone = leftInDz;
        s_xinputPrevLeftDir = leftDir;
        return;
    }

    const WORD changed = static_cast<WORD>(w ^ s_xinputPollPrevWButtons);
    const bool l2Edge = (l2Now != s_xinputPrevL2Pressed);
    const bool r2Edge = (r2Now != s_xinputPrevR2Pressed);

    const bool leftDzEdge = (leftInDz != s_xinputPrevLeftInDeadzone);
    const bool leftDirEdge =
        !leftInDz && !s_xinputPrevLeftInDeadzone && (leftDir != s_xinputPrevLeftDir);
    const bool stickEvent = leftDzEdge || leftDirEdge;

    if (changed == 0 && !l2Edge && !r2Edge && !stickEvent)
    {
        return;
    }

    for (const XInputDigitalButtonMapEntry& e : g_XInputDigitalMap)
    {
        if ((changed & e.mask) == 0)
        {
            continue;
        }
        const bool down = (w & e.mask) != 0;
        wchar_t line[256] = {};
        swprintf_s(line, _countof(line),
            L"XInput[slot=%u] id=%s label=\"%s\" %s\r\n",
            static_cast<unsigned int>(slot),
            GamepadButton_GetIdName(e.id),
            GamepadButton_GetDisplayLabel(e.id, kFamily),
            down ? L"down" : L"up");
        OutputDebugStringW(line);
    }

    if (l2Edge)
    {
        wchar_t line[256] = {};
        swprintf_s(line, _countof(line),
            L"XInput[slot=%u] id=%s label=\"%s\" %s value=%u\r\n",
            static_cast<unsigned int>(slot),
            GamepadButton_GetIdName(GamepadButtonId::L2),
            GamepadButton_GetDisplayLabel(GamepadButtonId::L2, kFamily),
            l2Now ? L"down" : L"up",
            static_cast<unsigned int>(lt));
        OutputDebugStringW(line);
    }

    if (r2Edge)
    {
        wchar_t line[256] = {};
        swprintf_s(line, _countof(line),
            L"XInput[slot=%u] id=%s label=\"%s\" %s value=%u\r\n",
            static_cast<unsigned int>(slot),
            GamepadButton_GetIdName(GamepadButtonId::R2),
            GamepadButton_GetDisplayLabel(GamepadButtonId::R2, kFamily),
            r2Now ? L"down" : L"up",
            static_cast<unsigned int>(rt));
        OutputDebugStringW(line);
    }

    if (stickEvent)
    {
        if (leftDzEdge)
        {
            wchar_t line[320] = {};
            if (leftInDz)
            {
                swprintf_s(line, _countof(line),
                    L"XInput[slot=%u] LeftStick dz=in raw=(%d,%d)\r\n",
                    static_cast<unsigned int>(slot),
                    static_cast<int>(lx),
                    static_cast<int>(ly));
            }
            else
            {
                swprintf_s(line, _countof(line),
                    L"XInput[slot=%u] LeftStick dz=out raw=(%d,%d) dir=%s\r\n",
                    static_cast<unsigned int>(slot),
                    static_cast<int>(lx),
                    static_cast<int>(ly),
                    Win32_LeftStickDirLabel(leftDir));
            }
            OutputDebugStringW(line);
        }
        else if (leftDirEdge)
        {
            wchar_t line[320] = {};
            swprintf_s(line, _countof(line),
                L"XInput[slot=%u] LeftStick dz=out raw=(%d,%d) dir=%s->%s\r\n",
                static_cast<unsigned int>(slot),
                static_cast<int>(lx),
                static_cast<int>(ly),
                Win32_LeftStickDirLabel(s_xinputPrevLeftDir),
                Win32_LeftStickDirLabel(leftDir));
            OutputDebugStringW(line);
        }
    }

    s_xinputPollPrevWButtons = w;
    s_xinputPrevL2Pressed = l2Now;
    s_xinputPrevR2Pressed = r2Now;
    s_xinputPrevLeftInDeadzone = leftInDz;
    s_xinputPrevLeftDir = leftDir;
}

static GameControllerKind Win32_ClassifyGameControllerKind(
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

static bool Win32_TryGetRawInputDeviceString(HANDLE hDevice, UINT infoType, wchar_t* buffer, size_t bufferCount)
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

static void Win32_LogRawInputHidGameControllersClassified()
{
    OutputDebugStringW(L"--- HID gamepads (Raw Input + classify) ---\r\n");

    const bool anyXInput = Win32_QueryAnyXInputConnected();

    UINT numDevices = 0;
    if (GetRawInputDeviceList(nullptr, &numDevices, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
    {
        OutputDebugStringW(L"GetRawInputDeviceList(count) failed\r\n");
        return;
    }
    if (numDevices == 0)
    {
        OutputDebugStringW(L"(no Raw Input devices)\r\n");
        return;
    }

    std::vector<RAWINPUTDEVICELIST> devices(numDevices);
    UINT copyCount = numDevices;
    if (GetRawInputDeviceList(devices.data(), &copyCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
    {
        OutputDebugStringW(L"GetRawInputDeviceList(list) failed\r\n");
        return;
    }

    for (UINT i = 0; i < copyCount; ++i)
    {
        if (devices[i].dwType != RIM_TYPEHID)
        {
            continue;
        }

        const HANDLE hDevice = devices[i].hDevice;

        RID_DEVICE_INFO info = {};
        info.cbSize = sizeof(info);
        UINT cbInfo = sizeof(info);
        if (GetRawInputDeviceInfo(hDevice, RIDI_DEVICEINFO, &info, &cbInfo) == static_cast<UINT>(-1))
        {
            continue;
        }
        if (info.dwType != RIM_TYPEHID)
        {
            continue;
        }

        GameControllerHidSummary traits = {};
        traits.device_info_valid = true;
        traits.vendor_id = static_cast<std::uint16_t>(info.hid.dwVendorId);
        traits.product_id = static_cast<std::uint16_t>(info.hid.dwProductId);
        traits.usage_page = info.hid.usUsagePage;
        traits.usage = info.hid.usUsage;

        const bool looksLikeGamepad =
            (traits.usage_page == 0x01 && (traits.usage == 0x04 || traits.usage == 0x05)) ||
            (traits.vendor_id == 0x045E || traits.vendor_id == 0x054C);

        if (!looksLikeGamepad)
        {
            continue;
        }

        wchar_t pathBuf[512] = {};
        wchar_t productBuf[256] = {};
        Win32_TryGetRawInputDeviceString(hDevice, RIDI_DEVICENAME, pathBuf, _countof(pathBuf));
        Win32_TryGetRawInputDeviceString(hDevice, RIDI_PRODUCTNAME, productBuf, _countof(productBuf));

        const wchar_t* productPtr = (productBuf[0] != L'\0') ? productBuf : nullptr;
        const wchar_t* pathPtr = (pathBuf[0] != L'\0') ? pathBuf : nullptr;

        const GameControllerKind kind = Win32_ClassifyGameControllerKind(traits, productPtr, pathPtr, anyXInput);

        wchar_t line[1024] = {};
        swprintf_s(line, _countof(line),
            L"Gamepad: kind=%s vid=0x%04X pid=0x%04X usage=0x%04X/0x%04X xinput_any=%d name=\"%s\" path=\"%s\"\r\n",
            Win32_GameControllerKindLabel(kind),
            static_cast<unsigned int>(traits.vendor_id),
            static_cast<unsigned int>(traits.product_id),
            static_cast<unsigned int>(traits.usage_page),
            static_cast<unsigned int>(traits.usage),
            anyXInput ? 1 : 0,
            productPtr ? productPtr : L"",
            pathPtr ? pathPtr : L"");
        OutputDebugStringW(line);
    }
}

static bool Win32_TryFillPhysicalKeyFromRawInput(HRAWINPUT hRaw, PhysicalKeyEvent& out)
{
    RAWINPUT raw = {};
    UINT cbSize = sizeof(raw);
    if (GetRawInputData(hRaw, RID_INPUT, &raw, &cbSize, sizeof(RAWINPUTHEADER)) == (UINT)-1)
    {
        return false;
    }
    if (raw.header.dwType != RIM_TYPEKEYBOARD)
    {
        return false;
    }

    const RAWKEYBOARD& kb = raw.data.keyboard;
    out.native_key_code = static_cast<std::uint16_t>(kb.VKey);
    out.scan_code = kb.MakeCode;
    out.is_extended_0 = (kb.Flags & RI_KEY_E0) != 0;
    out.is_extended_1 = (kb.Flags & RI_KEY_E1) != 0;
    out.is_key_up = (kb.Flags & RI_KEY_BREAK) != 0;
    return true;
}

static bool Win32_TryFillDisplayLabel(const PhysicalKeyEvent& ev, wchar_t* buffer, size_t bufferCount)
{
    if (bufferCount == 0)
    {
        return false;
    }
    buffer[0] = L'\0';

    const HKL hkl = GetKeyboardLayout(0);
    BYTE keyState[256] = {};
    const UINT vk = static_cast<UINT>(ev.native_key_code);
    const UINT scan = static_cast<UINT>(ev.scan_code & 0xFF);

    wchar_t unicodeBuf[8] = {};
    const int cchUnicode = static_cast<int>(_countof(unicodeBuf));
    int n = ToUnicodeEx(vk, scan, keyState, unicodeBuf, cchUnicode, 0, hkl);

    if (n > 0)
    {
        if (n >= cchUnicode)
        {
            n = cchUnicode - 1;
        }
        bool unicodeOk = false;
        if (n > 0)
        {
            unicodeOk = true;
            for (int i = 0; i < n; ++i)
            {
                const wchar_t c = unicodeBuf[i];
                if (c < 0x20 || c == 0x7F)
                {
                    unicodeOk = false;
                    break;
                }
            }
        }
        if (unicodeOk)
        {
            const size_t maxCopy = (bufferCount > 0) ? (bufferCount - 1) : 0;
            const size_t copyCount = (static_cast<size_t>(n) < maxCopy) ? static_cast<size_t>(n) : maxCopy;
            for (size_t i = 0; i < copyCount; ++i)
            {
                buffer[i] = unicodeBuf[i];
            }
            buffer[copyCount] = L'\0';
            return true;
        }
        buffer[0] = L'\0';
    }

    if (n < 0)
    {
        ToUnicodeEx(vk, scan, keyState, nullptr, 0, 0, hkl);
    }

    LPARAM lParam = static_cast<LPARAM>(1) | (static_cast<LPARAM>(ev.scan_code & 0xFF) << 16);
    if (ev.is_extended_0)
    {
        lParam |= (1 << 24);
    }

    const int cchKeyName = (bufferCount > static_cast<size_t>(INT_MAX))
        ? INT_MAX
        : static_cast<int>(bufferCount);
    const int gkn = GetKeyNameTextW(lParam, buffer, cchKeyName);
    return gkn > 0;
}

static void Win32_FillLayoutTag(wchar_t* buffer, size_t bufferCount)
{
    if (bufferCount == 0)
    {
        return;
    }
    const HKL hkl = GetKeyboardLayout(0);
    wchar_t klid[KL_NAMELENGTH] = {};
    if (GetKeyboardLayoutNameW(klid))
    {
        swprintf_s(buffer, bufferCount, L"KLID=%s HKL=%p", klid, hkl);
    }
    else
    {
        swprintf_s(buffer, bufferCount, L"KLID=(fail) HKL=%p", hkl);
    }
}

static void PhysicalKey_FormatDebugLine(const PhysicalKeyEvent& ev, const wchar_t* displayLabel, const wchar_t* layoutTag, wchar_t* buffer, size_t bufferCount)
{
    swprintf_s(buffer, bufferCount,
        L"PhysicalKey: native=0x%04X scan=0x%04X ext0=%d ext1=%d %s | layout=\"%s\" | label=\"%s\"\r\n",
        ev.native_key_code,
        ev.scan_code,
        ev.is_extended_0 ? 1 : 0,
        ev.is_extended_1 ? 1 : 0,
        ev.is_key_up ? L"break" : L"make",
        layoutTag ? layoutTag : L"",
        displayLabel ? displayLabel : L"");
}

//
//  関数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目的: メイン ウィンドウのメッセージを処理します。
//
//  WM_COMMAND  - アプリケーション メニューの処理
//  WM_INPUT    - Raw Input（物理キー）
//  WM_TIMER    - XInput デジタルボタン（T11）
//  WM_PAINT    - メイン ウィンドウを描画する
//  WM_DESTROY  - 中止メッセージを表示して戻る
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 選択されたメニューの解析:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_INPUT:
        {
            PhysicalKeyEvent ev{};
            if (Win32_TryFillPhysicalKeyFromRawInput(reinterpret_cast<HRAWINPUT>(lParam), ev))
            {
                const wchar_t* labelPtr = L"-";
                wchar_t labelBuf[64] = {};
                if (!ev.is_key_up)
                {
                    if (Win32_TryFillDisplayLabel(ev, labelBuf, _countof(labelBuf)))
                    {
                        labelPtr = labelBuf;
                    }
                    else
                    {
                        labelPtr = L"(none)";
                    }
                }
                wchar_t layoutTag[96] = {};
                Win32_FillLayoutTag(layoutTag, _countof(layoutTag));
                wchar_t line[512];
                PhysicalKey_FormatDebugLine(ev, labelPtr, layoutTag, line, _countof(line));
                OutputDebugStringW(line);
            }
        }
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_ID_XINPUT_POLL)
        {
            Win32_XInputPollDigitalEdgesOnTimer(hWnd);
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: HDC を使用する描画コードをここに追加してください...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID_XINPUT_POLL);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// バージョン情報ボックスのメッセージ ハンドラーです。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
