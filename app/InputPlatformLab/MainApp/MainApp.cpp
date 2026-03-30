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

#ifndef XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
#endif

// Windows 8.1+（targetver によっては未定義のため）
#ifndef RIDI_PRODUCTNAME
#define RIDI_PRODUCTNAME 0x20000007
#endif

// ---------------------------------------------------------------------------
// T25: MainApp.cpp 内の中立ロジック整理（見出し・配置のみ。挙動は変更しない）
//
// 将来の .h / .cpp 分割の目安（命名は例。未実施）:
//   [1] PhysicalKeyTypes.h / PhysicalKey_win32.cpp — PhysicalKeyEvent, Raw Input 由来のキー処理・表示ラベル
//   [2] GamepadLabels.h / GamepadFamily.cpp — GameControllerKind, GamepadButtonId, ラベル表・HID 分類
//   [3] VirtualInputSnapshot.h / VirtualInputHelpers.cpp — VirtualInputSnapshot, VirtualInput_* ヘルパー
//   [4] VirtualInputPolicy.h — VirtualInputPolicy*（MoveHeld / MenuEdges）
//   [5] VirtualInputConsumer.h — VirtualInputConsumerFrame, BuildFrame
//   [6] VirtualInputMenuSample.h — メニュー試作の state / events / Apply / Reset
//   [7] Win32_InputDebugLog.cpp — VirtualInput 系の OutputDebugString ログ（policy / consumer / menu）
//   [8] Win32_XInputIntegration.cpp — XInput ポーリング・デジタルマップ・スナップショット埋め込み・先頭スロット
//
// ここではまだやらない: 実ファイル分割、名前空間再設計、include の最適化、API の再命名
// ---------------------------------------------------------------------------

// === T25 [1] Physical key（中立） — 将来: PhysicalKeyTypes.h など ===
// プラットフォーム中立な物理キーイベント（将来 input/ 配下へ移設可能）
struct PhysicalKeyEvent
{
    std::uint16_t native_key_code; // Win32 では仮想キー（VK）に相当
    std::uint16_t scan_code;       // スキャンコード（拡張プレフィックスは is_extended_* と併用）
    bool is_extended_0;            // 拡張キー前置 E0 相当
    bool is_extended_1;            // 拡張キー前置 E1 相当
    bool is_key_up;                // 離上（break）なら true
};

// === T25 [2] Gamepad family / logical buttons / stick dir（中立） — 将来: GamepadLabels.h など ===
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

// === T25 [8] 型：XInput スロット列挙結果（中立） — 実装・ログは [8] ブロック側へ ===
// XInput スロット列挙結果（将来 input/ 配下へ移設可能）
struct ControllerSlotProbeResult
{
    std::uint8_t slot;        // 0..3
    bool connected;
    std::uint8_t type;        // XINPUT_CAPABILITIES::Type
    std::uint8_t sub_type;    // XINPUT_CAPABILITIES::SubType
    std::uint16_t flags;      // XINPUT_CAPABILITIES::Flags
};

// === T25 [3] VirtualInputSnapshot + helpers（中立） — 将来: VirtualInputSnapshot.h / VirtualInputHelpers.cpp ===
// 1 フレーム分の仮想入力（Win32 型なし。将来 input/ 配下へ移設可能）
struct VirtualInputSnapshot
{
    bool connected;
    GameControllerKind family;

    bool south;
    bool east;
    bool west;
    bool north;
    bool l1;
    bool r1;
    bool l3;
    bool r3;
    bool start;
    bool select;
    bool dpadUp;
    bool dpadDown;
    bool dpadLeft;
    bool dpadRight;

    bool l2Pressed;
    bool r2Pressed;
    std::uint8_t leftTriggerRaw;
    std::uint8_t rightTriggerRaw;

    std::int16_t leftStickX;
    std::int16_t leftStickY;
    std::int16_t rightStickX;
    std::int16_t rightStickY;

    bool leftInDeadzone;
    GamepadLeftStickDir leftDir;
    bool rightInDeadzone;
    GamepadLeftStickDir rightDir;
};

// T16: 中立ヘルパー（Win32 / XInput 型なし） — T25 [3] 続き
static void VirtualInput_ResetDisconnected(VirtualInputSnapshot& s)
{
    s = {};
    s.connected = false;
    s.family = GameControllerKind::Unknown;
    s.leftInDeadzone = true;
    s.rightInDeadzone = true;
}

static bool VirtualInput_IsButtonDown(const VirtualInputSnapshot& s, GamepadButtonId id)
{
    switch (id)
    {
    case GamepadButtonId::South: return s.south;
    case GamepadButtonId::East: return s.east;
    case GamepadButtonId::West: return s.west;
    case GamepadButtonId::North: return s.north;
    case GamepadButtonId::L1: return s.l1;
    case GamepadButtonId::R1: return s.r1;
    case GamepadButtonId::L2: return s.l2Pressed;
    case GamepadButtonId::R2: return s.r2Pressed;
    case GamepadButtonId::L3: return s.l3;
    case GamepadButtonId::R3: return s.r3;
    case GamepadButtonId::Start: return s.start;
    case GamepadButtonId::Select: return s.select;
    case GamepadButtonId::DPadUp: return s.dpadUp;
    case GamepadButtonId::DPadDown: return s.dpadDown;
    case GamepadButtonId::DPadLeft: return s.dpadLeft;
    case GamepadButtonId::DPadRight: return s.dpadRight;
    case GamepadButtonId::Count: break;
    }
    return false;
}

static bool VirtualInput_WasButtonPressed(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    GamepadButtonId id)
{
    return !VirtualInput_IsButtonDown(prev, id) && VirtualInput_IsButtonDown(curr, id);
}

static bool VirtualInput_WasButtonReleased(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    GamepadButtonId id)
{
    return VirtualInput_IsButtonDown(prev, id) && !VirtualInput_IsButtonDown(curr, id);
}

static bool VirtualInput_IsL2Pressed(const VirtualInputSnapshot& s)
{
    return s.l2Pressed;
}

static bool VirtualInput_IsR2Pressed(const VirtualInputSnapshot& s)
{
    return s.r2Pressed;
}

static GamepadLeftStickDir VirtualInput_GetLeftDir(const VirtualInputSnapshot& s)
{
    return s.leftDir;
}

static GamepadLeftStickDir VirtualInput_GetRightDir(const VirtualInputSnapshot& s)
{
    return s.rightDir;
}

static bool VirtualInput_LeftInDeadzone(const VirtualInputSnapshot& s)
{
    return s.leftInDeadzone;
}

static bool VirtualInput_RightInDeadzone(const VirtualInputSnapshot& s)
{
    return s.rightInDeadzone;
}

// === T25 [4] VirtualInputPolicy（中立） — 将来: VirtualInputPolicy.h ===
// T17/T19: 仮想入力の最小ポリシー（Win32 / XInput 非依存。VirtualInputSnapshot と既存 helper の上に載せる）
//
// 固定ルール:
// - Confirm = South の pressed（遷移 1 フレーム）
// - Cancel  = East の pressed
// - Menu    = Start の pressed
// - Move    = DPad 優先（1 つでも押されていれば DPad のみ）。なければ左スティック最小 4 方向
// - DPad    = 斜め合成あり（各軸 -1/0/+1 にクランプ）
// - Move は curr スナップショットから held として読む / メニュー系は prev→curr で pressed（遷移）として読む

struct VirtualInputPolicyHeld
{
    std::int8_t moveX;
    std::int8_t moveY;
};

struct VirtualInputPolicyMenuEdges
{
    bool confirm;
    bool cancel;
    bool menu;
};

static std::int8_t VirtualInputPolicy_ClampNeg1_0_1(int v)
{
    if (v < -1)
    {
        return -1;
    }
    if (v > 1)
    {
        return 1;
    }
    return static_cast<std::int8_t>(v);
}

static void VirtualInputPolicy_FillMoveFromDpad(const VirtualInputSnapshot& s, std::int8_t& outX, std::int8_t& outY)
{
    int x = 0;
    int y = 0;
    if (s.dpadLeft)
    {
        x -= 1;
    }
    if (s.dpadRight)
    {
        x += 1;
    }
    if (s.dpadUp)
    {
        y += 1;
    }
    if (s.dpadDown)
    {
        y -= 1;
    }
    outX = VirtualInputPolicy_ClampNeg1_0_1(x);
    outY = VirtualInputPolicy_ClampNeg1_0_1(y);
}

static void VirtualInputPolicy_FillMoveFromLeftStick(const VirtualInputSnapshot& s, std::int8_t& outX, std::int8_t& outY)
{
    outX = 0;
    outY = 0;
    switch (s.leftDir)
    {
    case GamepadLeftStickDir::Left: outX = -1; break;
    case GamepadLeftStickDir::Right: outX = 1; break;
    case GamepadLeftStickDir::Up: outY = 1; break;
    case GamepadLeftStickDir::Down: outY = -1; break;
    default: break;
    }
}

static VirtualInputPolicyHeld VirtualInputPolicy_MoveHeld(const VirtualInputSnapshot& s)
{
    VirtualInputPolicyHeld h{};
    const bool dpadAny =
        s.dpadUp || s.dpadDown || s.dpadLeft || s.dpadRight;
    if (dpadAny)
    {
        VirtualInputPolicy_FillMoveFromDpad(s, h.moveX, h.moveY);
    }
    else
    {
        VirtualInputPolicy_FillMoveFromLeftStick(s, h.moveX, h.moveY);
    }
    return h;
}

static VirtualInputPolicyMenuEdges VirtualInputPolicy_MenuEdges(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    VirtualInputPolicyMenuEdges e{};
    e.confirm = VirtualInput_WasButtonPressed(prev, curr, GamepadButtonId::South);
    e.cancel = VirtualInput_WasButtonPressed(prev, curr, GamepadButtonId::East);
    e.menu = VirtualInput_WasButtonPressed(prev, curr, GamepadButtonId::Start);
    return e;
}

// === T25 [5] VirtualInputConsumerFrame（中立） — 将来: VirtualInputConsumer.h ===
// T21: 消費側 1 フレーム分（VirtualInputSnapshot / XInput を知らない層向け。policy から組み立てる）
struct VirtualInputConsumerFrame
{
    std::int8_t moveX;
    std::int8_t moveY;
    bool confirmPressed;
    bool cancelPressed;
    bool menuPressed;
};

static VirtualInputConsumerFrame VirtualInputConsumer_BuildFrame(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    VirtualInputConsumerFrame f{};
    const VirtualInputPolicyHeld held = VirtualInputPolicy_MoveHeld(curr);
    f.moveX = held.moveX;
    f.moveY = held.moveY;
    const VirtualInputPolicyMenuEdges e = VirtualInputPolicy_MenuEdges(prev, curr);
    f.confirmPressed = e.confirm;
    f.cancelPressed = e.cancel;
    f.menuPressed = e.menu;
    return f;
}

// === T25 [6] Menu sample（state / events / apply / reset）（中立） — 将来: VirtualInputMenuSample.h ===
// T22/T23: 2x2 サンプルメニュー状態機械（入力は VirtualInputConsumerFrame のみ。状態更新は Win32 非依存）
//
// 固定仕様（ここ以外にロジックを書かない）:
// - menuPressed        -> menuOpen を toggle
// - confirmPressed     -> menuOpen のとき activate イベント（選択は変えない）
// - cancelPressed      -> cancel イベント。menuOpen なら false にし menuClosedByCancel
// - moveX/moveY        -> prev が 0 かつ今フレームが非 0 のときだけ 1 マス（repeat なし）
// - selectionX/Y       -> 0..1 にクランプ。menuOpen=false のときは移動しない
// - 毎フレーム末尾で prevMoveX/Y <- f.moveX/moveY（次フレームのエッジ検出用）
struct VirtualInputMenuSampleState
{
    bool menuOpen;
    std::int8_t selectionX;
    std::int8_t selectionY;
    std::int8_t prevMoveX;
    std::int8_t prevMoveY;
};

struct VirtualInputMenuSampleEvents
{
    bool menuToggled;
    bool selectionChanged;
    bool activated;
    bool cancelled;
    bool menuClosedByCancel;
};

static void VirtualInputMenuSample_Reset(VirtualInputMenuSampleState& s)
{
    s = {};
}

static std::int8_t VirtualInputMenuSample_ClampSelection(std::int8_t v)
{
    if (v < 0)
    {
        return 0;
    }
    if (v > 1)
    {
        return 1;
    }
    return v;
}

static VirtualInputMenuSampleEvents VirtualInputMenuSample_Apply(
    VirtualInputMenuSampleState& s,
    const VirtualInputConsumerFrame& f)
{
    VirtualInputMenuSampleEvents ev{};

    if (f.menuPressed)
    {
        s.menuOpen = !s.menuOpen;
        ev.menuToggled = true;
    }

    if (f.confirmPressed && s.menuOpen)
    {
        ev.activated = true;
    }

    if (f.cancelPressed)
    {
        ev.cancelled = true;
        if (s.menuOpen)
        {
            s.menuOpen = false;
            ev.menuClosedByCancel = true;
        }
    }

    if (s.menuOpen)
    {
        const std::int8_t osx = s.selectionX;
        const std::int8_t osy = s.selectionY;
        const bool mxEdge = (s.prevMoveX == 0 && f.moveX != 0);
        const bool myEdge = (s.prevMoveY == 0 && f.moveY != 0);
        if (mxEdge)
        {
            s.selectionX = VirtualInputMenuSample_ClampSelection(
                static_cast<std::int8_t>(s.selectionX + f.moveX));
        }
        if (myEdge)
        {
            s.selectionY = VirtualInputMenuSample_ClampSelection(
                static_cast<std::int8_t>(s.selectionY + f.moveY));
        }
        if (osx != s.selectionX || osy != s.selectionY)
        {
            ev.selectionChanged = true;
        }
    }

    s.prevMoveX = f.moveX;
    s.prevMoveY = f.moveY;
    return ev;
}

// === T25 [2] 補助：HID 属性（分類用。Raw Input 取得は [8]） — 将来: GamepadFamily.cpp など ===
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

// --- T25: 前方宣言（責務は上記 [1]〜[8] に対応。実装はファイル後半） ---
// このコード モジュールに含まれる関数の宣言を転送します:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

// [1] Physical key / keyboard label
static BOOL Win32_RegisterKeyboardRawInput(HWND hwnd);
static bool Win32_TryFillPhysicalKeyFromRawInput(HRAWINPUT hRaw, PhysicalKeyEvent& out);
static bool Win32_TryFillDisplayLabel(const PhysicalKeyEvent& ev, wchar_t* buffer, size_t bufferCount);
static void Win32_FillLayoutTag(wchar_t* buffer, size_t bufferCount);
static void PhysicalKey_FormatDebugLine(const PhysicalKeyEvent& ev, const wchar_t* displayLabel, const wchar_t* layoutTag, wchar_t* buffer, size_t bufferCount);

// [8] XInput slot probe / startup
static void Win32_FillControllerSlotProbe(std::uint8_t slot, ControllerSlotProbeResult& out);
static void Win32_LogControllerSlotProbeLine(const ControllerSlotProbeResult& probe);
static void Win32_LogXInputSlotsAtStartup();

// [2] family classify + [8] Raw Input HID 列挙
static bool Win32_QueryAnyXInputConnected();
static GameControllerKind Win32_ClassifyGameControllerKind(
    const GameControllerHidSummary& traits,
    const wchar_t* productName,
    const wchar_t* devicePath,
    bool anyXInputConnected);
static const wchar_t* Win32_GameControllerKindLabel(GameControllerKind kind);
static bool Win32_TryGetRawInputDeviceString(HANDLE hDevice, UINT infoType, wchar_t* buffer, size_t bufferCount);
static void Win32_LogRawInputHidGameControllersClassified();

// [2] Gamepad button label tables
static const wchar_t* GamepadButton_GetIdName(GamepadButtonId id);
static const wchar_t* GamepadButton_GetDisplayLabel(GamepadButtonId id, GameControllerKind family);
static void GamepadButton_LogLabelTablesAtStartup();

// [8] bridge + [7] VirtualInput ログ群
static void Win32_FillVirtualInputSnapshotFromXInputState(const XINPUT_STATE& st, VirtualInputSnapshot& out);
static void Win32_LogVirtualInputSnapshotSummary(const VirtualInputSnapshot& snap, DWORD slot);
static void Win32_LogVirtualInputHelperProbe(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    DWORD slot);
static void Win32_LogVirtualInputPolicyIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr);
static void Win32_LogVirtualInputConsumerIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr);
static void Win32_LogVirtualInputMenuSampleIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr);
static void Win32_LogVirtualInputMenuSample_Events(
    const VirtualInputMenuSampleEvents& ev,
    const VirtualInputMenuSampleState& s);
static void Win32_LogVirtualInputMenuSample_StateDumpIfChanged(
    const VirtualInputMenuSampleState& s);

// [8] タイマー XInput ポーリング（先頭スロット）
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

// === T25 [1] Win32: Raw Input キーボード登録（読み取り・ラベル整形はファイル後半の [1] 続き） ===
static BOOL Win32_RegisterKeyboardRawInput(HWND hwnd)
{
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x06;
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

// === T25 [8] Win32: XInput スロット列挙・接続確認（先頭接続スロット前提の土台） ===
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

// === T25 [2] Gamepad: family ラベル・論理ボタン名・表示ラベル表（将来: GamepadLabels.cpp） ===
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

// === T25 [8] XInput デジタルマスク → GamepadButtonId（ポーリング・エッジログと共有） ===
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

// T25 [8] タイマー駆動ポーリング用の static（VirtualInput / メニュー試作のランタイム状態）
static DWORD s_xinputPollPrevSlot = XUSER_MAX_COUNT;
static WORD s_xinputPollPrevWButtons = 0;
static bool s_xinputPrevL2Pressed = false;
static bool s_xinputPrevR2Pressed = false;
static bool s_xinputPrevLeftInDeadzone = true;
static GamepadLeftStickDir s_xinputPrevLeftDir = GamepadLeftStickDir::None;
static bool s_xinputPrevRightInDeadzone = true;
static GamepadLeftStickDir s_xinputPrevRightDir = GamepadLeftStickDir::None;
static UINT s_virtualInputSnapshotLogCounter = 0;
static VirtualInputSnapshot s_virtualInputPrev{};
static VirtualInputSnapshot s_virtualInputCurr{};
static VirtualInputConsumerFrame s_virtualInputConsumerPrev{};
static bool s_virtualInputConsumerHasPrev = false;
static VirtualInputMenuSampleState s_virtualInputMenuSampleState{};
static VirtualInputMenuSampleState s_virtualInputMenuSampleDumpPrev{};
static bool s_virtualInputMenuSampleDumpHasPrev = false;

// === T25 [8] Win32: スティックデッドゾーン・方向（XInput 生値 → 中立 enum） ===
static bool Win32_LeftStickInDeadzone(SHORT x, SHORT y)
{
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double mag = std::sqrt(dx * dx + dy * dy);
    return mag < static_cast<double>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
}

static bool Win32_RightStickInDeadzone(SHORT x, SHORT y)
{
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double mag = std::sqrt(dx * dx + dy * dy);
    return mag < static_cast<double>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
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

// === T25 [8]→[3] 橋渡し: XINPUT_STATE → VirtualInputSnapshot（中立型への詰め替え） ===
static void Win32_FillVirtualInputSnapshotFromXInputState(const XINPUT_STATE& st, VirtualInputSnapshot& out)
{
    out.connected = true;
    out.family = GameControllerKind::Xbox;

    const WORD wb = st.Gamepad.wButtons;
    out.south = (wb & XINPUT_GAMEPAD_A) != 0;
    out.east = (wb & XINPUT_GAMEPAD_B) != 0;
    out.west = (wb & XINPUT_GAMEPAD_X) != 0;
    out.north = (wb & XINPUT_GAMEPAD_Y) != 0;
    out.l1 = (wb & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    out.r1 = (wb & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    out.l3 = (wb & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    out.r3 = (wb & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    out.start = (wb & XINPUT_GAMEPAD_START) != 0;
    out.select = (wb & XINPUT_GAMEPAD_BACK) != 0;
    out.dpadUp = (wb & XINPUT_GAMEPAD_DPAD_UP) != 0;
    out.dpadDown = (wb & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    out.dpadLeft = (wb & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    out.dpadRight = (wb & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    out.leftTriggerRaw = st.Gamepad.bLeftTrigger;
    out.rightTriggerRaw = st.Gamepad.bRightTrigger;
    out.l2Pressed = (out.leftTriggerRaw >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    out.r2Pressed = (out.rightTriggerRaw >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

    out.leftStickX = st.Gamepad.sThumbLX;
    out.leftStickY = st.Gamepad.sThumbLY;
    out.rightStickX = st.Gamepad.sThumbRX;
    out.rightStickY = st.Gamepad.sThumbRY;

    out.leftInDeadzone = Win32_LeftStickInDeadzone(st.Gamepad.sThumbLX, st.Gamepad.sThumbLY);
    out.leftDir = Win32_ClassifyLeftStickDir(st.Gamepad.sThumbLX, st.Gamepad.sThumbLY, out.leftInDeadzone);
    out.rightInDeadzone = Win32_RightStickInDeadzone(st.Gamepad.sThumbRX, st.Gamepad.sThumbRY);
    out.rightDir = Win32_ClassifyLeftStickDir(st.Gamepad.sThumbRX, st.Gamepad.sThumbRY, out.rightInDeadzone);
}

// === T25 [7] Win32: VirtualInput 系デバッグログ（snapshot / helper / policy / consumer / menu） ===
static void Win32_LogVirtualInputSnapshotSummary(const VirtualInputSnapshot& s, DWORD slot)
{
    wchar_t line[768] = {};
    swprintf_s(line, _countof(line),
        L"VirtualInput slot=%u fam=%s conn=%d "
        L"faceABXY=%d%d%d%d L1R1=%d%d L2R2=%d/%d raw=%u/%u L3R3=%d%d StSel=%d%d "
        L"Dpad=%d%d%d%d "
        L"L(%d,%d)z=%d Ldir=%s R(%d,%d)z=%d Rdir=%s\r\n",
        static_cast<unsigned int>(slot),
        Win32_GameControllerKindLabel(s.family),
        s.connected ? 1 : 0,
        s.south ? 1 : 0,
        s.east ? 1 : 0,
        s.west ? 1 : 0,
        s.north ? 1 : 0,
        s.l1 ? 1 : 0,
        s.r1 ? 1 : 0,
        s.l2Pressed ? 1 : 0,
        s.r2Pressed ? 1 : 0,
        static_cast<unsigned int>(s.leftTriggerRaw),
        static_cast<unsigned int>(s.rightTriggerRaw),
        s.l3 ? 1 : 0,
        s.r3 ? 1 : 0,
        s.start ? 1 : 0,
        s.select ? 1 : 0,
        s.dpadUp ? 1 : 0,
        s.dpadDown ? 1 : 0,
        s.dpadLeft ? 1 : 0,
        s.dpadRight ? 1 : 0,
        static_cast<int>(s.leftStickX),
        static_cast<int>(s.leftStickY),
        s.leftInDeadzone ? 1 : 0,
        Win32_LeftStickDirLabel(s.leftDir),
        static_cast<int>(s.rightStickX),
        static_cast<int>(s.rightStickY),
        s.rightInDeadzone ? 1 : 0,
        Win32_LeftStickDirLabel(s.rightDir));
    OutputDebugStringW(line);
}

static void Win32_LogVirtualInputHelperProbe(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    DWORD slot)
{
    wchar_t line[512] = {};
    swprintf_s(line, _countof(line),
        L"VirtualInputHelper[slot=%u] SouthDown=%d "
        L"Pressed(South)=%d Released(South)=%d "
        L"L2=%d R2=%d Ldz=%d Rdz=%d Ldir=%s Rdir=%s\r\n",
        static_cast<unsigned int>(slot),
        VirtualInput_IsButtonDown(curr, GamepadButtonId::South) ? 1 : 0,
        VirtualInput_WasButtonPressed(prev, curr, GamepadButtonId::South) ? 1 : 0,
        VirtualInput_WasButtonReleased(prev, curr, GamepadButtonId::South) ? 1 : 0,
        VirtualInput_IsL2Pressed(curr) ? 1 : 0,
        VirtualInput_IsR2Pressed(curr) ? 1 : 0,
        VirtualInput_LeftInDeadzone(curr) ? 1 : 0,
        VirtualInput_RightInDeadzone(curr) ? 1 : 0,
        Win32_LeftStickDirLabel(VirtualInput_GetLeftDir(curr)),
        Win32_LeftStickDirLabel(VirtualInput_GetRightDir(curr)));
    OutputDebugStringW(line);
}

static void Win32_LogVirtualInputPolicyIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    const VirtualInputPolicyHeld pm = VirtualInputPolicy_MoveHeld(prev);
    const VirtualInputPolicyHeld cm = VirtualInputPolicy_MoveHeld(curr);
    const bool moveChanged =
        (pm.moveX != cm.moveX) || (pm.moveY != cm.moveY);

    if (moveChanged)
    {
        wchar_t line[160] = {};
        swprintf_s(line, _countof(line),
            L"VirtualInputPolicy moveHeld=(%d,%d)\r\n",
            static_cast<int>(cm.moveX),
            static_cast<int>(cm.moveY));
        OutputDebugStringW(line);
    }

    const VirtualInputPolicyMenuEdges e = VirtualInputPolicy_MenuEdges(prev, curr);
    if (e.confirm || e.cancel || e.menu)
    {
        wchar_t line[192] = {};
        swprintf_s(line, _countof(line),
            L"VirtualInputPolicy edges confirm=%d cancel=%d menu=%d\r\n",
            e.confirm ? 1 : 0,
            e.cancel ? 1 : 0,
            e.menu ? 1 : 0);
        OutputDebugStringW(line);
    }
}

static void Win32_LogVirtualInputConsumerIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    const VirtualInputConsumerFrame now = VirtualInputConsumer_BuildFrame(prev, curr);
    if (!s_virtualInputConsumerHasPrev)
    {
        s_virtualInputConsumerPrev = now;
        s_virtualInputConsumerHasPrev = true;
        return;
    }

    const bool changed =
        s_virtualInputConsumerPrev.moveX != now.moveX ||
        s_virtualInputConsumerPrev.moveY != now.moveY ||
        s_virtualInputConsumerPrev.confirmPressed != now.confirmPressed ||
        s_virtualInputConsumerPrev.cancelPressed != now.cancelPressed ||
        s_virtualInputConsumerPrev.menuPressed != now.menuPressed;
    if (!changed)
    {
        return;
    }

    wchar_t line[256] = {};
    swprintf_s(line, _countof(line),
        L"VirtualInputConsumer move=(%d,%d) confirm=%d cancel=%d menu=%d\r\n",
        static_cast<int>(now.moveX),
        static_cast<int>(now.moveY),
        now.confirmPressed ? 1 : 0,
        now.cancelPressed ? 1 : 0,
        now.menuPressed ? 1 : 0);
    OutputDebugStringW(line);
    s_virtualInputConsumerPrev = now;
}

static void Win32_LogVirtualInputMenuSample_Events(
    const VirtualInputMenuSampleEvents& ev,
    const VirtualInputMenuSampleState& s)
{
    if (ev.menuToggled)
    {
        wchar_t line[128] = {};
        swprintf_s(line, _countof(line),
            L"VirtualInputMenuSample menuOpen=%d\r\n",
            s.menuOpen ? 1 : 0);
        OutputDebugStringW(line);
    }
    if (ev.selectionChanged)
    {
        wchar_t line[128] = {};
        swprintf_s(line, _countof(line),
            L"VirtualInputMenuSample selection=(%d,%d)\r\n",
            static_cast<int>(s.selectionX),
            static_cast<int>(s.selectionY));
        OutputDebugStringW(line);
    }
    if (ev.activated)
    {
        wchar_t line[128] = {};
        swprintf_s(line, _countof(line),
            L"VirtualInputMenuSample activate=(%d,%d)\r\n",
            static_cast<int>(s.selectionX),
            static_cast<int>(s.selectionY));
        OutputDebugStringW(line);
    }
    if (ev.cancelled)
    {
        OutputDebugStringW(L"VirtualInputMenuSample cancel\r\n");
    }
    if (ev.menuClosedByCancel)
    {
        OutputDebugStringW(L"VirtualInputMenuSample menuOpen=0\r\n");
    }
}

// T24: メニューサンプル状態の 1 行ダンプ（Apply/Reset は変更しない。state 変化時のみ）
static void Win32_LogVirtualInputMenuSample_StateDumpIfChanged(
    const VirtualInputMenuSampleState& s)
{
    if (!s_virtualInputMenuSampleDumpHasPrev)
    {
        s_virtualInputMenuSampleDumpPrev = s;
        s_virtualInputMenuSampleDumpHasPrev = true;
        return;
    }

    const bool stateChanged =
        s.menuOpen != s_virtualInputMenuSampleDumpPrev.menuOpen ||
        s.selectionX != s_virtualInputMenuSampleDumpPrev.selectionX ||
        s.selectionY != s_virtualInputMenuSampleDumpPrev.selectionY ||
        s.prevMoveX != s_virtualInputMenuSampleDumpPrev.prevMoveX ||
        s.prevMoveY != s_virtualInputMenuSampleDumpPrev.prevMoveY;
    if (!stateChanged)
    {
        return;
    }

    wchar_t line[192] = {};
    swprintf_s(line, _countof(line),
        L"VirtualInputMenuSampleDump open=%d sel=(%d,%d) prevMove=(%d,%d) changed=1\r\n",
        s.menuOpen ? 1 : 0,
        static_cast<int>(s.selectionX),
        static_cast<int>(s.selectionY),
        static_cast<int>(s.prevMoveX),
        static_cast<int>(s.prevMoveY));
    OutputDebugStringW(line);
    s_virtualInputMenuSampleDumpPrev = s;
}

static void Win32_LogVirtualInputMenuSampleIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    const VirtualInputConsumerFrame f = VirtualInputConsumer_BuildFrame(prev, curr);
    const VirtualInputMenuSampleEvents ev =
        VirtualInputMenuSample_Apply(s_virtualInputMenuSampleState, f);
    Win32_LogVirtualInputMenuSample_Events(ev, s_virtualInputMenuSampleState);
    Win32_LogVirtualInputMenuSample_StateDumpIfChanged(s_virtualInputMenuSampleState);
}

// === T25 [8] Win32: 先頭接続スロット取得 + タイマーでの XInput 統合（VirtualInput 更新・エッジログ） ===
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
        s_xinputPrevRightInDeadzone = true;
        s_xinputPrevRightDir = GamepadLeftStickDir::None;
        s_virtualInputSnapshotLogCounter = 0;
        VirtualInput_ResetDisconnected(s_virtualInputPrev);
        VirtualInput_ResetDisconnected(s_virtualInputCurr);
        s_virtualInputConsumerHasPrev = false;
        VirtualInputMenuSample_Reset(s_virtualInputMenuSampleState);
        s_virtualInputMenuSampleDumpHasPrev = false;
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
        s_xinputPrevRightInDeadzone = true;
        s_xinputPrevRightDir = GamepadLeftStickDir::None;
        s_virtualInputSnapshotLogCounter = 0;
        VirtualInput_ResetDisconnected(s_virtualInputPrev);
        VirtualInput_ResetDisconnected(s_virtualInputCurr);
        s_virtualInputConsumerHasPrev = false;
        VirtualInputMenuSample_Reset(s_virtualInputMenuSampleState);
        s_virtualInputMenuSampleDumpHasPrev = false;
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

    const SHORT rx = state.Gamepad.sThumbRX;
    const SHORT ry = state.Gamepad.sThumbRY;
    const bool rightInDz = Win32_RightStickInDeadzone(rx, ry);
    const GamepadLeftStickDir rightDir = Win32_ClassifyLeftStickDir(rx, ry, rightInDz);

    s_virtualInputPrev = s_virtualInputCurr;
    Win32_FillVirtualInputSnapshotFromXInputState(state, s_virtualInputCurr);
    if ((++s_virtualInputSnapshotLogCounter % 60) == 0)
    {
        Win32_LogVirtualInputSnapshotSummary(s_virtualInputCurr, slot);
    }

    if (slot != s_xinputPollPrevSlot)
    {
        s_xinputPollPrevSlot = slot;
        s_xinputPollPrevWButtons = w;
        s_xinputPrevL2Pressed = l2Now;
        s_xinputPrevR2Pressed = r2Now;
        s_xinputPrevLeftInDeadzone = leftInDz;
        s_xinputPrevLeftDir = leftDir;
        s_xinputPrevRightInDeadzone = rightInDz;
        s_xinputPrevRightDir = rightDir;
        s_virtualInputConsumerHasPrev = false;
        VirtualInputMenuSample_Reset(s_virtualInputMenuSampleState);
        s_virtualInputMenuSampleDumpHasPrev = false;
        return;
    }

    Win32_LogVirtualInputPolicyIfChanged(s_virtualInputPrev, s_virtualInputCurr);
    Win32_LogVirtualInputConsumerIfChanged(s_virtualInputPrev, s_virtualInputCurr);
    Win32_LogVirtualInputMenuSampleIfChanged(s_virtualInputPrev, s_virtualInputCurr);

    const WORD changed = static_cast<WORD>(w ^ s_xinputPollPrevWButtons);
    const bool l2Edge = (l2Now != s_xinputPrevL2Pressed);
    const bool r2Edge = (r2Now != s_xinputPrevR2Pressed);

    const bool leftDzEdge = (leftInDz != s_xinputPrevLeftInDeadzone);
    const bool leftDirEdge =
        !leftInDz && !s_xinputPrevLeftInDeadzone && (leftDir != s_xinputPrevLeftDir);
    const bool rightDzEdge = (rightInDz != s_xinputPrevRightInDeadzone);
    const bool rightDirEdge =
        !rightInDz && !s_xinputPrevRightInDeadzone && (rightDir != s_xinputPrevRightDir);
    const bool stickEvent = leftDzEdge || leftDirEdge || rightDzEdge || rightDirEdge;

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
        VirtualInputSnapshot snapCurr{};
        if (e.id == GamepadButtonId::South)
        {
            Win32_FillVirtualInputSnapshotFromXInputState(state, snapCurr);
        }
        wchar_t line[256] = {};
        swprintf_s(line, _countof(line),
            L"XInput[slot=%u] id=%s label=\"%s\" %s\r\n",
            static_cast<unsigned int>(slot),
            GamepadButton_GetIdName(e.id),
            GamepadButton_GetDisplayLabel(e.id, kFamily),
            down ? L"down" : L"up");
        OutputDebugStringW(line);
        if (e.id == GamepadButtonId::South)
        {
            Win32_LogVirtualInputHelperProbe(s_virtualInputPrev, snapCurr, slot);
        }
    }

    if (l2Edge)
    {
        VirtualInputSnapshot snapCurr{};
        Win32_FillVirtualInputSnapshotFromXInputState(state, snapCurr);
        wchar_t line[256] = {};
        swprintf_s(line, _countof(line),
            L"XInput[slot=%u] id=%s label=\"%s\" %s value=%u\r\n",
            static_cast<unsigned int>(slot),
            GamepadButton_GetIdName(GamepadButtonId::L2),
            GamepadButton_GetDisplayLabel(GamepadButtonId::L2, kFamily),
            l2Now ? L"down" : L"up",
            static_cast<unsigned int>(lt));
        OutputDebugStringW(line);
        Win32_LogVirtualInputHelperProbe(s_virtualInputPrev, snapCurr, slot);
    }

    if (r2Edge)
    {
        VirtualInputSnapshot snapCurr{};
        Win32_FillVirtualInputSnapshotFromXInputState(state, snapCurr);
        wchar_t line[256] = {};
        swprintf_s(line, _countof(line),
            L"XInput[slot=%u] id=%s label=\"%s\" %s value=%u\r\n",
            static_cast<unsigned int>(slot),
            GamepadButton_GetIdName(GamepadButtonId::R2),
            GamepadButton_GetDisplayLabel(GamepadButtonId::R2, kFamily),
            r2Now ? L"down" : L"up",
            static_cast<unsigned int>(rt));
        OutputDebugStringW(line);
        Win32_LogVirtualInputHelperProbe(s_virtualInputPrev, snapCurr, slot);
    }

    if (stickEvent)
    {
        if (leftDzEdge)
        {
            VirtualInputSnapshot snapCurr{};
            Win32_FillVirtualInputSnapshotFromXInputState(state, snapCurr);
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
            Win32_LogVirtualInputHelperProbe(s_virtualInputPrev, snapCurr, slot);
        }
        else if (leftDirEdge)
        {
            VirtualInputSnapshot snapCurr{};
            Win32_FillVirtualInputSnapshotFromXInputState(state, snapCurr);
            wchar_t line[320] = {};
            swprintf_s(line, _countof(line),
                L"XInput[slot=%u] LeftStick dz=out raw=(%d,%d) dir=%s->%s\r\n",
                static_cast<unsigned int>(slot),
                static_cast<int>(lx),
                static_cast<int>(ly),
                Win32_LeftStickDirLabel(s_xinputPrevLeftDir),
                Win32_LeftStickDirLabel(leftDir));
            OutputDebugStringW(line);
            Win32_LogVirtualInputHelperProbe(s_virtualInputPrev, snapCurr, slot);
        }

        if (rightDzEdge)
        {
            VirtualInputSnapshot snapCurr{};
            Win32_FillVirtualInputSnapshotFromXInputState(state, snapCurr);
            wchar_t line[320] = {};
            if (rightInDz)
            {
                swprintf_s(line, _countof(line),
                    L"XInput[slot=%u] axis=RightStick dz=in raw=(%d,%d)\r\n",
                    static_cast<unsigned int>(slot),
                    static_cast<int>(rx),
                    static_cast<int>(ry));
            }
            else
            {
                swprintf_s(line, _countof(line),
                    L"XInput[slot=%u] axis=RightStick dz=out raw=(%d,%d) dir=%s\r\n",
                    static_cast<unsigned int>(slot),
                    static_cast<int>(rx),
                    static_cast<int>(ry),
                    Win32_LeftStickDirLabel(rightDir));
            }
            OutputDebugStringW(line);
            Win32_LogVirtualInputHelperProbe(s_virtualInputPrev, snapCurr, slot);
        }
        else if (rightDirEdge)
        {
            VirtualInputSnapshot snapCurr{};
            Win32_FillVirtualInputSnapshotFromXInputState(state, snapCurr);
            wchar_t line[320] = {};
            swprintf_s(line, _countof(line),
                L"XInput[slot=%u] axis=RightStick dz=out raw=(%d,%d) dir=%s->%s\r\n",
                static_cast<unsigned int>(slot),
                static_cast<int>(rx),
                static_cast<int>(ry),
                Win32_LeftStickDirLabel(s_xinputPrevRightDir),
                Win32_LeftStickDirLabel(rightDir));
            OutputDebugStringW(line);
            Win32_LogVirtualInputHelperProbe(s_virtualInputPrev, snapCurr, slot);
        }
    }

    s_xinputPollPrevWButtons = w;
    s_xinputPrevL2Pressed = l2Now;
    s_xinputPrevR2Pressed = r2Now;
    s_xinputPrevLeftInDeadzone = leftInDz;
    s_xinputPrevLeftDir = leftDir;
    s_xinputPrevRightInDeadzone = rightInDz;
    s_xinputPrevRightDir = rightDir;
}

// === T25 [2] GameControllerKind 推定（HID + 名称パス） — Raw Input 起動ログから利用 ===
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

// === T25 [8] Win32: Raw Input デバイス文字列取得 + HID ゲームパッド列挙ログ ===
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

// === T25 [1] 続き: PhysicalKeyEvent の Raw Input 取得・表示ラベル・デバッグ行 ===
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
