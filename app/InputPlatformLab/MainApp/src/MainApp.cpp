// MainApp.cpp : アプリケーションのエントリ ポイントを定義します。
//

#include "framework.h"
#include "MainApp.h"
#include "InputCore.h"
#include "WindowsAppShell.h"
#include "WindowsInputBackend.h"
#include "WindowsDisplayBackend.h"
#include "WindowsRenderer.h"
#include "Win32DebugOverlay.h"

#include <windowsx.h>

#include <cstdint>
#include <cstring>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <stdio.h>
#include <set>
#include <string>
#include <vector>

#include <xinput.h>
#pragma comment(lib, "Xinput.lib")

#include <shellscalingapi.h>
#pragma comment(lib, "Shcore.lib")

#ifndef USER_DEFAULT_SCREEN_DPI
#define USER_DEFAULT_SCREEN_DPI 96
#endif

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

#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400u
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
    UINT16 native_key_code; // Win32 では仮想キー（VK）に相当
    UINT16 scan_code;       // スキャンコード（拡張プレフィックスは is_extended_* と併用）
    bool is_extended_0;            // 拡張キー前置 E0 相当
    bool is_extended_1;            // 拡張キー前置 E1 相当
    bool is_key_up;                // 離上（break）なら true
};

// === T25 [2] Parser / Support / HID 分類（中立） — T21: ControllerClassification.h / ControllerClassification.cpp
// === T25 [8] 型：XInput スロット列挙結果（中立） — 実装・ログは [8] ブロック側へ ===
// XInput スロット列挙結果（将来 input/ 配下へ移設可能）
struct ControllerSlotProbeResult
{
    UINT8 slot;        // 0..3
    bool connected;
    UINT8 type;        // XINPUT_CAPABILITIES::Type
    UINT8 sub_type;    // XINPUT_CAPABILITIES::SubType
    UINT16 flags;      // XINPUT_CAPABILITIES::Flags
};

// T20: VirtualInputSnapshot / policy / keyboard→consumer — VirtualInputNeutral.h / VirtualInputNeutral.cpp

static KeyboardActionState s_keyboardActionState{};
static KeyboardActionState s_keyboardActionStateAtLastTimer{};

// グローバル変数:
HINSTANCE hInst;                                // 現在のインターフェイス
WCHAR szTitle[MAX_LOADSTRING];                  // タイトル バーのテキスト
WCHAR szWindowClass[MAX_LOADSTRING];            // メイン ウィンドウ クラス名
static HWND s_mainWindowHwnd = nullptr;         // T13: サンプル画面 InvalidateRect 用
static WindowsRendererState s_windowsRendererState{}; // T31: Init / Resize / Frame 経由で使用

// T14: モニター / 解像度列挙（起動時 1 回キャッシュ。neutral ヘッダは触らない）
struct DisplayModeInfo
{
    int width;
    int height;
    int bits_per_pixel;
    int refresh_hz;
};

struct DisplayMonitorInfo
{
    HMONITOR hMonitor = nullptr;
    std::wstring device_name;
    RECT monitor_rect;
    RECT work_rect;
    bool is_primary;
    std::vector<DisplayModeInfo> modes;
};

static std::vector<DisplayMonitorInfo> s_displayMonitorsCache;

// T14 UI: 表示は visibleModeCount 行のみ。VirtualInputMenuSample の menuOpen 中は矢印は 2x2 メニュー用のため T14 の選択は動かさない（Tab で menu トグル）。
static constexpr bool kT14KeyboardSelDebugLog = false; // true で [T14sel] 1 行（回帰確認用）
static constexpr size_t kT14VisibleModeCount = 8;
static constexpr size_t kT14SelectedMonitorIndex = 0;
static size_t s_t14SelectedModeIndex = 0;
static size_t s_t14FirstVisibleModeIndex = 0;

// T15: 希望解像度 → 最近似 mode（列挙キャッシュのみ。適用はしない）
struct DisplayModeMatchResult
{
    size_t nearestModeIndex; // 無効時は (size_t)-1
    bool exactMatch;
    int deltaW;
    int deltaH;
};

struct DisplayResolutionPreset
{
    int width;
    int height;
};

static constexpr DisplayResolutionPreset kT15DesiredPresets[] = {
    {1280, 720},
    {1366, 768},
    {1600, 900},
    {1920, 1080},
    {2560, 1440},
    {3440, 1440},
    {3840, 2160},
    {4096, 2160},
};

static constexpr size_t kT15DesiredPresetCount =
    sizeof(kT15DesiredPresets) / sizeof(kT15DesiredPresets[0]);

static size_t s_t15DesiredPresetIndex = 0;
static int s_t15DesiredWidth = 0;
static int s_t15DesiredHeight = 0;
static DisplayModeMatchResult s_t15MatchResult{};

// T16/T17: ウィンドウ再生成パラメータ
struct MainWindowConfig
{
    int clientWidth;  // windowed: logical client（DPI 変換後・work クランプ後）
    int clientHeight; // logical
    DWORD windowStyle;
    DWORD windowExStyle;
    BOOL useAdjustWindowRect; // TRUE のとき AdjustWindowRectExForDpi で外枠を算出
    UINT dpiX;
    UINT dpiY;
    BOOL fillMonitorPhysical; // TRUE: CreateWindow は monitor 矩形を物理ピクセルでそのまま使う
    int createPhysicalX;
    int createPhysicalY;
    int createPhysicalW;
    int createPhysicalH;
};

// T17: Windowed / Borderless / Fullscreen（monitor 0 固定）
enum class T17PresentationMode : UINT8
{
    Windowed = 0,
    Borderless = 1,
    Fullscreen = 2,
};

static T17PresentationMode s_t17CurrentPresentationMode = T17PresentationMode::Windowed;
static T17PresentationMode s_t17LastAppliedPresentationMode = T17PresentationMode::Windowed;
static LONG s_t17LastFullscreenChangeResult = DISP_CHANGE_SUCCESSFUL;
static bool s_t17LastWindowApplySuccess = false;
static bool s_t17FullscreenDisplayAppliedNow = false;

static unsigned s_t17ApplySeq = 0;
static unsigned s_t17CycleSeq = 0;
static wchar_t s_t17LastActionLine[512] = L"(none yet)";
static wchar_t s_t17LastKeyAffectingT17[160] = L"(none)";
static wchar_t s_t17LastEnterCandidateToApplied[256] = L"(no Enter apply yet)";
static wchar_t s_t17F5UnrelatedHint[192] = L"";
// Enter の短いタップはタイマー 1 周期内で make/break となり edge が取りこぼすため、WM_INPUT の make でラッチしタイマーで 1 回だけ消費する。
static bool s_t17PendingApplyRequest = false;

// T18: コントローラー識別（1 台・Raw Input 先頭 HID + XInput 先頭スロット）
struct T18ControllerIdentifySnapshot
{
    int xinput_slot; // -1: 未接続
    bool hid_found;
    GameControllerHidSummary hid;
    wchar_t device_path[512];
    wchar_t product_name[256];
    GameControllerKind inferred_kind;
    ControllerParserKind parser_kind;
    ControllerSupportLevel support_level;
};
static T18ControllerIdentifySnapshot s_t18{};
static T18ControllerIdentifySnapshot s_t18LogPrev{};
static bool s_t18HasLogPrev = false;

// T14〜T18 のまとまり（このファイル内のサンプル用デバッグ範囲）:
//   T14: 列挙モードの選択・表示（T15 近傍の * 表示など）
//   T15: 希望プリセットに対する最近傍（列挙結果の参照のみ。モード変更の直接適用ではない）
//   T16: DPI・クライアントサイズ・ウィンドウ再生成
//   T17: Windowed / Borderless / Fullscreen（F6 で候補循環、Enter で適用）
//   T18: 画面上の「1 台」向け識別（Raw Input 先頭の HID ゲームパッド + XInput 先頭スロット）

// メイン画面デバッグ表示の縦スクロール（T13〜T18 全体）。Win32DebugOverlay.cpp と共有。
int s_paintScrollY = 0;
int s_paintScrollLinePx = 16; // WM_PAINT で TEXTMETRIC から更新
int s_paintDbgContentHeight = 0;
int s_paintDbgContentHeightBase = 0; // DrawText 計測のみ（パディング前）
int s_paintDbgExtraBottomPadding = 0; // Borderless/Fullscreen 時の仮想下余白
int s_paintDbgClientHeight = 0;
int s_paintDbgT17DocY = 0; // ドキュメント座標で「--- T17 presentation ---」行の先頭

// T14 auto-follow: WM_PAINT のレイアウト計測と同一の値（タイマー側は再計測しない）
bool s_paintDbgT14LayoutValid = false;
int s_paintDbgT14VisibleModesDocStartY = 0;
int s_paintDbgLineHeight = 16;
int s_paintDbgActualOverlayHeight = 0;
int s_paintDbgClientW = 0;
int s_paintDbgClientH = 0;
int s_paintDbgMaxScroll = 0;

#ifndef WIN32_MAIN_DEBUG_SCROLL_LINE
#define WIN32_MAIN_DEBUG_SCROLL_LINE 16
#endif
// T25: 1 にすると WM_PAINT で D3D clear のみ（GDI テキストなし。clear 色の確認用）
#ifndef WIN32_MAIN_D3D_CLEAR_ONLY_PAINT
#define WIN32_MAIN_D3D_CLEAR_ONLY_PAINT 0
#endif
// T33: 1 にすると GDI debug overlay を抑止し、D3D+D2D の 1 行のみ比較しやすくする。
#ifndef WIN32_MAIN_T33_HIDE_GDI_OVERLAY
#define WIN32_MAIN_T33_HIDE_GDI_OVERLAY 0
#endif
// F7: T17 行を画面上端付近へ（ピッタリだと窮屈なので上余白）
#ifndef WIN32_MAIN_T17_JUMP_TOP_MARGIN
#define WIN32_MAIN_T17_JUMP_TOP_MARGIN 160
#endif
// PageUp/PageDown: 1 ページではなく nPage のこの分率だけ移動（キー・スクロールバー共通）
#ifndef WIN32_MAIN_SCROLL_PAGEUP_NUM
#define WIN32_MAIN_SCROLL_PAGEUP_NUM 1
#endif
#ifndef WIN32_MAIN_SCROLL_PAGEUP_DEN
#define WIN32_MAIN_SCROLL_PAGEUP_DEN 2
#endif
// T14 auto-follow: 選択行上端をクライアント上端から (topMargin + N×lineHeight) に揃える（N=2 → 上余白の下から 3 行目付近）
#ifndef WIN32_T14_AUTOFOLLOW_ANCHOR_ROWS
#define WIN32_T14_AUTOFOLLOW_ANCHOR_ROWS 2
#endif

static void Win32_T17_ApplyCurrentPresentationMode(HWND hwnd);

enum class T16RecreateStatus
{
    None,
    Ok,
    Fail,
};

static T16RecreateStatus s_t16RecreateStatus = T16RecreateStatus::None;
static int s_t16LastRequestedOuterW = 0;
static int s_t16LastRequestedOuterH = 0;
static int s_t16LastActualOuterW = 0;
static int s_t16LastActualOuterH = 0;
static int s_t16LastTargetPhysicalW = 0;
static int s_t16LastTargetPhysicalH = 0;
// デバッググリッド: Enter（T17 apply）成功時だけ更新。T14 カーソル移動では変えない。
static int s_lastCommittedGridSelectedPhysW = 0;
static int s_lastCommittedGridSelectedPhysH = 0;
// WindowsRenderer.cpp の [GRID] mode=... 1 回ログ用（Release LTCG で TU 内関数が落ちるのを避け MainApp で定義）
extern bool s_loggedGridOnce;
void WindowsRenderer_DebugGrid_ResetLogOnce(void)
{
    s_loggedGridOnce = false;
}
static int s_t16LastTargetLogicalW = 0;
static int s_t16LastTargetLogicalH = 0;
static UINT s_t16LastDpiX = USER_DEFAULT_SCREEN_DPI;
static UINT s_t16LastDpiY = USER_DEFAULT_SCREEN_DPI;
static bool s_t16DestroyIsRecreate = false;

enum class T16WindowedTargetModeSource
{
    T14SelectedList,
    T15NearestFallback,
};

static T16WindowedTargetModeSource s_t16WindowedTargetModeSource =
    T16WindowedTargetModeSource::T15NearestFallback;
static size_t s_t16WindowedTargetT14ListIndex = static_cast<size_t>(-1);
static DisplayModeInfo s_t16WindowedTargetMode{};

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
static void Win32_FillControllerSlotProbe(UINT8 slot, ControllerSlotProbeResult& out);
static void Win32_LogControllerSlotProbeLine(const ControllerSlotProbeResult& probe);
static void Win32_LogXInputSlotsAtStartup();

// [2] family classify + [8] Raw Input HID 列挙
static bool Win32_QueryAnyXInputConnected();
static const wchar_t* Win32_GameControllerKindLabel(GameControllerKind kind);
static bool Win32_TryGetRawInputDeviceString(HANDLE hDevice, UINT infoType, wchar_t* buffer, size_t bufferCount);
static void Win32_LogRawInputHidGameControllersClassified();

static const wchar_t* Win32_GameControllerKindFamilyLabel(GameControllerKind kind);
static void Win32_T18_RefreshControllerIdentifySnapshot();
static void Win32_T18_AppendPaintSection(wchar_t* buf, size_t bufCount);

// [2] Gamepad button label tables
static const wchar_t* GamepadButton_GetIdName(GamepadButtonId id);
static const wchar_t* GamepadButton_GetDisplayLabel(GamepadButtonId id, GameControllerKind family);
static void GamepadButton_LogLabelTablesAtStartup();

// [8] bridge + [7] VirtualInput ログ群
static void Win32_FillVirtualInputSnapshotFromXInputState(const XINPUT_STATE& st, VirtualInputSnapshot& out);
static void Win32_LogVirtualInputSnapshotSummary(const VirtualInputSnapshot& snap, DWORD slot);
static void Win32_LogVirtualInputPs4Slot99ShoulderGroupIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    DWORD slot);
static void Win32_LogVirtualInputPs4Slot99IsolateEdges(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr);
static void Win32_LogPs4Slot99BridgeDeltaIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    DWORD slot);
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
    const VirtualInputConsumerFrame& unifiedFrame,
    HWND hwndForPaint);
static void Win32_UnifiedInputConsumerMenuTick(HWND hwndForPaint);
static void Win32_LogVirtualInputMenuSample_Events(
    const VirtualInputMenuSampleEvents& ev,
    const VirtualInputMenuSampleState& s);
static void Win32_LogVirtualInputMenuSample_StateDumpIfChanged(
    const VirtualInputMenuSampleState& s);

// [8] タイマー XInput ポーリング（先頭スロット）
static DWORD Win32_GetFirstConnectedXInputSlotOrMax();
static void Win32_XInputPollDigitalEdgesOnTimer(HWND hwnd);

// WndProc から切り出した薄いハンドラ（挙動は元 case と同一）
static void Win32_WndProc_OnRawInput(LPARAM lParam);
static void Win32_WndProc_OnXInputPollTimer(HWND hWnd);
static void Win32_WndProc_OnClientSize(HWND hWnd);
static bool Win32_WndProc_OnVScroll(HWND hWnd, WPARAM wParam, LPARAM lParam, UINT message, LRESULT* outDefResult);
static void Win32_WndProc_OnMouseWheel(HWND hWnd, WPARAM wParam);
static void Win32_MainView_PaintFrame(HWND hWnd);
static void Win32_WndProc_OnPaint(HWND hWnd);

// Win32_FillMenuSamplePaintBuffers の分割（T14〜T17 本文 + T18 追記）
static void Win32_FillMenuSamplePaintBuffers_MenuColumn(
    wchar_t* menuBuf,
    size_t menuBufCount,
    bool compactT37);
static void Win32_FillMenuSamplePaintBuffers_T14T15Column(wchar_t* t14Buf, size_t t14BufCount);
static void Win32_FillMenuSamplePaintBuffers_AppendT16T18T17(
    HWND hwnd,
    const RECT& rcClient,
    wchar_t* t14Buf,
    size_t t14BufCount);

// menuOpen / 統合 Consumer の境界
static void Win32_UnifiedInputMenuTick_ClearPendingT17IfMenuOpen(void);
static void Win32_UnifiedInputMenuTick_WhenMenuClosed(HWND hwndForPaint);
static void Win32_UnifiedInputMenuTick_MergeAndApply(HWND hwndForPaint);

static void Win32_InitProcessDpiAwareness()
{
    BOOL ok = FALSE;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFn)(DPI_AWARENESS_CONTEXT);
        auto pSet = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (pSet)
        {
            ok = pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
    if (!ok)
    {
        SetProcessDPIAware();
        OutputDebugStringW(
            L"[T16] SetProcessDpiAwarenessContext(PMv2) unavailable; using SetProcessDPIAware\r\n");
    }
    else
    {
        OutputDebugStringW(L"[T16] DPI awareness: Per-Monitor Aware V2\r\n");
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Win32_InitProcessDpiAwareness();

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

// ---------------------------------------------------------------------------
// T14: ディスプレイ列挙（モニターと解像度のキャッシュ）
// ---------------------------------------------------------------------------

static BOOL CALLBACK Win32_DisplayMonitorEnumProc(
    HMONITOR hMonitor,
    HDC hdcMonitor,
    LPRECT lprcMonitor,
    LPARAM dwData)
{
    UNREFERENCED_PARAMETER(hdcMonitor);
    UNREFERENCED_PARAMETER(lprcMonitor);
    UNREFERENCED_PARAMETER(dwData);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(hMonitor, reinterpret_cast<LPMONITORINFO>(&mi)))
    {
        return TRUE;
    }

    DisplayMonitorInfo mon{};
    mon.hMonitor = hMonitor;
    mon.device_name = mi.szDevice;
    mon.monitor_rect = mi.rcMonitor;
    mon.work_rect = mi.rcWork;
    mon.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    for (DWORD modeIndex = 0;; ++modeIndex)
    {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(mi.szDevice, modeIndex, &dm))
        {
            break;
        }
        const int w = static_cast<int>(dm.dmPelsWidth);
        const int h = static_cast<int>(dm.dmPelsHeight);
        if (w <= 0 || h <= 0)
        {
            continue;
        }
        DisplayModeInfo m{};
        m.width = w;
        m.height = h;
        m.bits_per_pixel = static_cast<int>(dm.dmBitsPerPel);
        m.refresh_hz = static_cast<int>(dm.dmDisplayFrequency);
        mon.modes.push_back(m);
    }

    std::vector<DisplayModeInfo> deduped;
    std::set<std::pair<int, int>> seenWh;
    for (const DisplayModeInfo& m : mon.modes)
    {
        const auto key = std::make_pair(m.width, m.height);
        if (seenWh.insert(key).second)
        {
            deduped.push_back(m);
        }
    }
    mon.modes = std::move(deduped);

    s_displayMonitorsCache.push_back(std::move(mon));
    return TRUE;
}

// 起動時などに 1 回、モニターと EnumDisplaySettings のモード一覧を詰める。
static void Win32_DisplayEnumerateAndCache()
{
    s_displayMonitorsCache.clear();
    EnumDisplayMonitors(nullptr, nullptr, Win32_DisplayMonitorEnumProc, 0);
}

static void Win32_LogDisplayMonitors()
{
    OutputDebugStringW(L"--- T14 display monitors (EnumDisplayMonitors / EnumDisplaySettingsW) ---\r\n");
    for (size_t i = 0; i < s_displayMonitorsCache.size(); ++i)
    {
        const DisplayMonitorInfo& mon = s_displayMonitorsCache[i];
        wchar_t line[512] = {};
        swprintf_s(line, _countof(line),
            L"[T14] monitor[%zu] device=\"%s\" primary=%d "
            L"monitor=(%d,%d)-(%d,%d) work=(%d,%d)-(%d,%d)\r\n",
            i,
            mon.device_name.c_str(),
            mon.is_primary ? 1 : 0,
            static_cast<int>(mon.monitor_rect.left),
            static_cast<int>(mon.monitor_rect.top),
            static_cast<int>(mon.monitor_rect.right),
            static_cast<int>(mon.monitor_rect.bottom),
            static_cast<int>(mon.work_rect.left),
            static_cast<int>(mon.work_rect.top),
            static_cast<int>(mon.work_rect.right),
            static_cast<int>(mon.work_rect.bottom));
        OutputDebugStringW(line);
        for (size_t j = 0; j < mon.modes.size(); ++j)
        {
            const DisplayModeInfo& mode = mon.modes[j];
            swprintf_s(line, _countof(line),
                L"[T14]   mode[%zu] %dx%d bpp=%d hz=%d\r\n",
                j,
                mode.width,
                mode.height,
                mode.bits_per_pixel,
                mode.refresh_hz);
            OutputDebugStringW(line);
        }
    }
    OutputDebugStringW(L"--- T14 end ---\r\n");
}

// 列挙直後に選択インデックスと「表示ウィンドウ先頭」を有効範囲に収める。
static void Win32_T14_ClampIndicesAfterEnumerate()
{
    if (s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        s_t14SelectedModeIndex = 0;
        s_t14FirstVisibleModeIndex = 0;
        return;
    }
    const size_t n = s_displayMonitorsCache[kT14SelectedMonitorIndex].modes.size();
    if (n == 0)
    {
        s_t14SelectedModeIndex = 0;
        s_t14FirstVisibleModeIndex = 0;
        return;
    }
    if (s_t14SelectedModeIndex >= n)
    {
        s_t14SelectedModeIndex = n - 1;
    }
    const size_t maxFirst =
        (n > kT14VisibleModeCount) ? (n - kT14VisibleModeCount) : 0;
    if (s_t14FirstVisibleModeIndex > maxFirst)
    {
        s_t14FirstVisibleModeIndex = maxFirst;
    }
    if (s_t14SelectedModeIndex < s_t14FirstVisibleModeIndex)
    {
        s_t14FirstVisibleModeIndex = s_t14SelectedModeIndex;
    }
    if (s_t14SelectedModeIndex >= s_t14FirstVisibleModeIndex + kT14VisibleModeCount)
    {
        s_t14FirstVisibleModeIndex = s_t14SelectedModeIndex - (kT14VisibleModeCount - 1);
    }
}

static void Win32_T14_TryAutoScrollSelectionIntoView(HWND hwnd);

// menuOpen でないときのみ Up/Down エッジで呼ぶ。変化時のみ InvalidateRect。
static void Win32_T14_TryScrollFromKeyboardEdges(bool upEdge, bool downEdge, HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }
    if (s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        return;
    }
    const size_t n = s_displayMonitorsCache[kT14SelectedMonitorIndex].modes.size();
    if (n == 0)
    {
        return;
    }

    const size_t oldSel = s_t14SelectedModeIndex;
    size_t newSel = oldSel;
    if (upEdge && newSel > 0)
    {
        --newSel;
    }
    else if (downEdge && newSel + 1 < n)
    {
        ++newSel;
    }
    if (newSel == oldSel)
    {
        return;
    }

    s_t14SelectedModeIndex = newSel;

    if (s_t14SelectedModeIndex < s_t14FirstVisibleModeIndex)
    {
        s_t14FirstVisibleModeIndex = s_t14SelectedModeIndex;
    }
    if (s_t14SelectedModeIndex >= s_t14FirstVisibleModeIndex + kT14VisibleModeCount)
    {
        s_t14FirstVisibleModeIndex = s_t14SelectedModeIndex - (kT14VisibleModeCount - 1);
    }

    if (kT14KeyboardSelDebugLog)
    {
        wchar_t sel[192] = {};
        swprintf_s(
            sel,
            _countof(sel),
            L"[T14sel] up=%d down=%d old=%zu new=%zu first=%zu\r\n",
            upEdge ? 1 : 0,
            downEdge ? 1 : 0,
            oldSel,
            newSel,
            s_t14FirstVisibleModeIndex);
        OutputDebugStringW(sel);
    }

    Win32_T14_TryAutoScrollSelectionIntoView(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// T15: 希望解像度プリセット → 最近傍モード（スコア比較。実ディスプレイへの適用は T17 側）
// T15 non-exact: 主に W/H のズレ（縦を強め）と総ピクセル差。アスペクト差はタイブレーク。
// ---------------------------------------------------------------------------
static constexpr int kT15HeightMismatchWeightSq = 4;

static bool Win32_T15_IsBetterMatch(
    bool exactNew,
    long long weightedDistSqNew,
    long long areaDiffNew,
    double arDiffNew,
    int hzNew,
    bool exactBest,
    long long weightedDistSqBest,
    long long areaDiffBest,
    double arDiffBest,
    int hzBest)
{
    if (exactNew != exactBest)
    {
        return exactNew;
    }
    if (exactNew && exactBest)
    {
        return hzNew > hzBest;
    }
    if (weightedDistSqNew < weightedDistSqBest)
    {
        return true;
    }
    if (weightedDistSqNew > weightedDistSqBest)
    {
        return false;
    }
    if (areaDiffNew < areaDiffBest)
    {
        return true;
    }
    if (areaDiffNew > areaDiffBest)
    {
        return false;
    }
    if (arDiffNew < arDiffBest)
    {
        return true;
    }
    if (arDiffNew > arDiffBest)
    {
        return false;
    }
    return hzNew > hzBest;
}

static DisplayModeMatchResult Win32_FindNearestDisplayMode(
    const std::vector<DisplayModeInfo>& modes,
    int desiredW,
    int desiredH)
{
    DisplayModeMatchResult r{};
    r.nearestModeIndex = static_cast<size_t>(-1);
    r.exactMatch = false;
    r.deltaW = 0;
    r.deltaH = 0;
    if (modes.empty() || desiredW <= 0 || desiredH <= 0)
    {
        return r;
    }

    const double desireAR = static_cast<double>(desiredW) / static_cast<double>(desiredH);
    const long long kH = static_cast<long long>(kT15HeightMismatchWeightSq);

    const DisplayModeInfo& b0 = modes[0];
    size_t bestIdx = 0;
    bool bestExact = (b0.width == desiredW && b0.height == desiredH);
    const double ar0 =
        static_cast<double>(b0.width) / static_cast<double>(b0.height);
    double bestArDiff = std::fabs(ar0 - desireAR);
    const long long dw0 = static_cast<long long>(b0.width) - desiredW;
    const long long dh0 = static_cast<long long>(b0.height) - desiredH;
    long long bestWeightedDistSq = dw0 * dw0 + kH * dh0 * dh0;
    long long bestAreaDiff =
        std::llabs(static_cast<long long>(b0.width) * b0.height -
                   static_cast<long long>(desiredW) * desiredH);
    int bestHz = b0.refresh_hz;

    for (size_t i = 1; i < modes.size(); ++i)
    {
        const DisplayModeInfo& m = modes[i];
        const bool exact = (m.width == desiredW && m.height == desiredH);
        const double arM =
            static_cast<double>(m.width) / static_cast<double>(m.height);
        const double arDiff = std::fabs(arM - desireAR);
        const long long dw = static_cast<long long>(m.width) - desiredW;
        const long long dh = static_cast<long long>(m.height) - desiredH;
        const long long weightedDistSq = dw * dw + kH * dh * dh;
        const long long areaDiff =
            std::llabs(static_cast<long long>(m.width) * m.height -
                       static_cast<long long>(desiredW) * desiredH);

        if (Win32_T15_IsBetterMatch(
                exact,
                weightedDistSq,
                areaDiff,
                arDiff,
                m.refresh_hz,
                bestExact,
                bestWeightedDistSq,
                bestAreaDiff,
                bestArDiff,
                bestHz))
        {
            bestIdx = i;
            bestExact = exact;
            bestArDiff = arDiff;
            bestWeightedDistSq = weightedDistSq;
            bestAreaDiff = areaDiff;
            bestHz = m.refresh_hz;
        }
    }

    const DisplayModeInfo& bm = modes[bestIdx];
    r.nearestModeIndex = bestIdx;
    r.exactMatch = bestExact;
    r.deltaW = bm.width - desiredW;
    r.deltaH = bm.height - desiredH;
    return r;
}

static void Win32_T15_ApplyDesiredPresetAndRecompute()
{
    if (kT15DesiredPresetCount == 0)
    {
        s_t15DesiredWidth = 0;
        s_t15DesiredHeight = 0;
        s_t15MatchResult = {};
        s_t15MatchResult.nearestModeIndex = static_cast<size_t>(-1);
        return;
    }
    if (s_t15DesiredPresetIndex >= kT15DesiredPresetCount)
    {
        s_t15DesiredPresetIndex = 0;
    }
    s_t15DesiredWidth = kT15DesiredPresets[s_t15DesiredPresetIndex].width;
    s_t15DesiredHeight = kT15DesiredPresets[s_t15DesiredPresetIndex].height;

    if (s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        s_t15MatchResult = {};
        s_t15MatchResult.nearestModeIndex = static_cast<size_t>(-1);
        return;
    }
    const std::vector<DisplayModeInfo>& modes =
        s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
    s_t15MatchResult =
        Win32_FindNearestDisplayMode(modes, s_t15DesiredWidth, s_t15DesiredHeight);
}

static void Win32_T15_LogDesiredNearestLine()
{
    wchar_t line[384] = {};
    if (s_t15MatchResult.nearestModeIndex == static_cast<size_t>(-1) ||
        s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        swprintf_s(
            line,
            _countof(line),
            L"[T15] desired %dx%d preset[%zu] -> nearest (none)\r\n",
            s_t15DesiredWidth,
            s_t15DesiredHeight,
            s_t15DesiredPresetIndex);
        OutputDebugStringW(line);
        return;
    }
    const std::vector<DisplayModeInfo>& modes =
        s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
    const size_t ni = s_t15MatchResult.nearestModeIndex;
    if (ni >= modes.size())
    {
        swprintf_s(
            line,
            _countof(line),
            L"[T15] desired %dx%d preset[%zu] -> nearest (invalid idx)\r\n",
            s_t15DesiredWidth,
            s_t15DesiredHeight,
            s_t15DesiredPresetIndex);
        OutputDebugStringW(line);
        return;
    }
    const DisplayModeInfo& nm = modes[ni];
    swprintf_s(
        line,
        _countof(line),
        L"[T15] desired %dx%d -> nearest[%zu] %dx%d bpp=%d hz=%d exact=%d dW=%d dH=%d\r\n",
        s_t15DesiredWidth,
        s_t15DesiredHeight,
        ni,
        nm.width,
        nm.height,
        nm.bits_per_pixel,
        nm.refresh_hz,
        s_t15MatchResult.exactMatch ? 1 : 0,
        s_t15MatchResult.deltaW,
        s_t15MatchResult.deltaH);
    OutputDebugStringW(line);
}

static void Win32_T15_TryChangePresetFromKeyboardEdges(bool leftEdge, bool rightEdge, HWND hwnd)
{
    if (!hwnd || kT15DesiredPresetCount == 0)
    {
        return;
    }
    const size_t oldPreset = s_t15DesiredPresetIndex;
    if (leftEdge && !rightEdge)
    {
        s_t15DesiredPresetIndex =
            (s_t15DesiredPresetIndex == 0) ? (kT15DesiredPresetCount - 1)
                                           : (s_t15DesiredPresetIndex - 1);
    }
    else if (rightEdge && !leftEdge)
    {
        s_t15DesiredPresetIndex = (s_t15DesiredPresetIndex + 1) % kT15DesiredPresetCount;
    }
    else
    {
        return;
    }
    if (s_t15DesiredPresetIndex == oldPreset)
    {
        return;
    }
    Win32_T15_ApplyDesiredPresetAndRecompute();
    Win32_T15_LogDesiredNearestLine();
    InvalidateRect(hwnd, nullptr, FALSE);
}

static bool Win32_T14_GetSelectedDisplayModeInfo(DisplayModeInfo& outMode, size_t& outListIndex)
{
    if (s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        return false;
    }
    const std::vector<DisplayModeInfo>& modes =
        s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
    if (modes.empty() || s_t14SelectedModeIndex >= modes.size())
    {
        return false;
    }
    outListIndex = s_t14SelectedModeIndex;
    outMode = modes[s_t14SelectedModeIndex];
    return true;
}

// ---------------------------------------------------------------------------
// T16: ウィンドウのクライアント／外枠・再生成（T14 選択または T15 フォールバックを物理サイズに解決）
// T17: 表示モード（下の Win32_T17_*）。T16 再生成の入口は Win32_T16_RecreateMainWindowFromCurrentSelection など。
// ---------------------------------------------------------------------------

static bool Win32_T16_GetTargetClientSizeFromNearestMode(int& outW, int& outH)
{
    if (s_t15MatchResult.nearestModeIndex == static_cast<size_t>(-1))
    {
        return false;
    }
    if (s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        return false;
    }
    const std::vector<DisplayModeInfo>& modes =
        s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
    if (s_t15MatchResult.nearestModeIndex >= modes.size())
    {
        return false;
    }
    const DisplayModeInfo& m = modes[s_t15MatchResult.nearestModeIndex];
    outW = m.width;
    outH = m.height;
    return true;
}

static const wchar_t* Win32_T16_WindowedModeSourceLabel(T16WindowedTargetModeSource src)
{
    switch (src)
    {
    case T16WindowedTargetModeSource::T14SelectedList:
        return L"T14 selected";
    case T16WindowedTargetModeSource::T15NearestFallback:
        return L"T15 nearest fallback";
    default:
        return L"?";
    }
}

static bool Win32_T16_ResolveWindowedTargetPhysicalSize(int& outW, int& outH, bool logToDebug)
{
    DisplayModeInfo sel{};
    size_t listIdx = 0;
    if (Win32_T14_GetSelectedDisplayModeInfo(sel, listIdx))
    {
        outW = sel.width;
        outH = sel.height;
        s_t16WindowedTargetModeSource = T16WindowedTargetModeSource::T14SelectedList;
        s_t16WindowedTargetT14ListIndex = listIdx;
        s_t16WindowedTargetMode = sel;
        if (logToDebug)
        {
            wchar_t line[320] = {};
            swprintf_s(
                line,
                _countof(line),
                L"[T16] mode source=selected list index=%zu\r\n",
                listIdx);
            OutputDebugStringW(line);
            swprintf_s(
                line,
                _countof(line),
                L"[T16] target mode from T14 selected: %dx%d bpp=%d hz=%d\r\n",
                sel.width,
                sel.height,
                sel.bits_per_pixel,
                sel.refresh_hz);
            OutputDebugStringW(line);
        }
        return true;
    }
    if (!Win32_T16_GetTargetClientSizeFromNearestMode(outW, outH))
    {
        return false;
    }
    s_t16WindowedTargetModeSource = T16WindowedTargetModeSource::T15NearestFallback;
    s_t16WindowedTargetT14ListIndex = static_cast<size_t>(-1);
    if (s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        return false;
    }
    const std::vector<DisplayModeInfo>& modes =
        s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
    if (s_t15MatchResult.nearestModeIndex >= modes.size())
    {
        return false;
    }
    s_t16WindowedTargetMode = modes[s_t15MatchResult.nearestModeIndex];
    if (logToDebug)
    {
        wchar_t line[320] = {};
        swprintf_s(
            line,
            _countof(line),
            L"[T16] mode source=fallback nearest (T15 index=%zu)\r\n",
            s_t15MatchResult.nearestModeIndex);
        OutputDebugStringW(line);
        swprintf_s(
            line,
            _countof(line),
            L"[T16] target mode from T15 nearest: %dx%d bpp=%d hz=%d\r\n",
            s_t16WindowedTargetMode.width,
            s_t16WindowedTargetMode.height,
            s_t16WindowedTargetMode.bits_per_pixel,
            s_t16WindowedTargetMode.refresh_hz);
        OutputDebugStringW(line);
    }
    return true;
}

// デバッググリッド: 分母は Enter 時に保存した s_lastCommittedGrid* のみ（ここでは T14 を再参照しない）。
static bool s_loggedGridClientDenomFallbackOnce = false;

static WindowsRendererPresentationMode Win32_MapT17ToRendererPresentationMode(T17PresentationMode m)
{
    switch (m)
    {
    case T17PresentationMode::Windowed:
        return WindowsRendererPresentationMode::Windowed;
    case T17PresentationMode::Borderless:
        return WindowsRendererPresentationMode::Borderless;
    case T17PresentationMode::Fullscreen:
        return WindowsRendererPresentationMode::Fullscreen;
    default:
        return WindowsRendererPresentationMode::Windowed;
    }
}

// T37: T34/T36 オフスクリーン有効時、本文を committed 仮想解像度基準で DWrite 描画（失敗時は GDI 本文にフォールバック）
static void Win32_T37_PrepareVirtualBodyOverlay(HWND hwnd)
{
    WindowsRendererState& st = s_windowsRendererState;
    st.t37VirtualBodyOverlayRequested = false;
    st.t37VirtualBodyOverlayRenderedOk = false;
    st.t37BodyText[0] = L'\0';
    st.t37ScrollVirtualPx = 0;

    const bool borderlessPath =
        (s_t17LastAppliedPresentationMode == T17PresentationMode::Borderless) && st.borderlessOffscreenComposite;
    const bool fullscreenPath =
        (s_t17LastAppliedPresentationMode == T17PresentationMode::Fullscreen) && st.fullscreenOffscreenComposite;
    if (!borderlessPath && !fullscreenPath)
    {
        return;
    }

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int ch = static_cast<int>(rc.bottom - rc.top);

    wchar_t menuBuf[3072] = {};
    wchar_t t14Buf[8192] = {};
    Win32_FillMenuSamplePaintBuffers(
        hwnd,
        rc,
        menuBuf,
        _countof(menuBuf),
        t14Buf,
        _countof(t14Buf),
        false);
    // T14 本文は final backbuffer の D2D HUD（左列）に描く。composite 上のスケール本文は出さない。
    st.t37BodyText[0] = L'\0';

    const int gh = s_lastCommittedGridSelectedPhysH;
    if (ch > 0 && gh > 0)
    {
        st.t37ScrollVirtualPx = static_cast<int>(
            static_cast<double>(s_paintScrollY) * static_cast<double>(gh) / static_cast<double>(ch));
    }
    else
    {
        st.t37ScrollVirtualPx = 0;
    }
    st.t37VirtualBodyOverlayRequested = true;
}

static void Win32_RefreshRendererGridDebugParams(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT cw = static_cast<UINT>((std::max)(0, static_cast<int>(rc.right - rc.left)));
    const UINT ch = static_cast<UINT>((std::max)(0, static_cast<int>(rc.bottom - rc.top)));

    // WM_PAINT が WM_SIZE より先に来ると swapchain/backbuffer の client が実クライアントより古いままになる。
    // composite の viewport と GetClientRect を毎フレーム一致させる。
    if (s_windowsRendererState.initialized && cw >= 1u && ch >= 1u &&
        (cw != s_windowsRendererState.clientWidth || ch != s_windowsRendererState.clientHeight))
    {
        WindowsRenderer_Resize(&s_windowsRendererState, cw, ch);
    }

    const int gw = s_lastCommittedGridSelectedPhysW;
    const int gh = s_lastCommittedGridSelectedPhysH;

    s_windowsRendererState.gridDebugClientPhysW = cw;
    s_windowsRendererState.gridDebugClientPhysH = ch;
    s_windowsRendererState.gridDebugCommittedPhysW = static_cast<std::uint32_t>((std::max)(0, gw));
    s_windowsRendererState.gridDebugCommittedPhysH = static_cast<std::uint32_t>((std::max)(0, gh));

    // T34/T35: Borderless のみ T34 オフスクリーン。それ以外では毎フレームフラグと RT を捨てる
    if (s_t17LastAppliedPresentationMode != T17PresentationMode::Borderless)
    {
        WindowsRenderer_ClearBorderlessOffscreen(&s_windowsRendererState);
    }
    else
    {
        s_windowsRendererState.borderlessOffscreenComposite = (gw > 0 && gh > 0);
        s_windowsRendererState.borderlessOffscreenPhysW =
            (gw > 0) ? static_cast<std::uint32_t>(gw) : 0u;
        s_windowsRendererState.borderlessOffscreenPhysH =
            (gh > 0) ? static_cast<std::uint32_t>(gh) : 0u;
    }

    // T36（実験）: Fullscreen のみ committed オフスクリーン。Borderless の T34 と別リソース。
    if (s_t17LastAppliedPresentationMode != T17PresentationMode::Fullscreen)
    {
        WindowsRenderer_ClearFullscreenOffscreen(&s_windowsRendererState);
    }
    else
    {
        s_windowsRendererState.fullscreenOffscreenComposite = (gw > 0 && gh > 0);
        s_windowsRendererState.fullscreenOffscreenPhysW =
            (gw > 0) ? static_cast<std::uint32_t>(gw) : 0u;
        s_windowsRendererState.fullscreenOffscreenPhysH =
            (gh > 0) ? static_cast<std::uint32_t>(gh) : 0u;
    }

    if (gw > 0 && gh > 0)
    {
        s_windowsRendererState.gridDebugBasis = WindowsRendererGridDebugBasis::CommittedSelected;
        s_windowsRendererState.gridDebugDenomPhysW = static_cast<std::uint32_t>(gw);
        s_loggedGridClientDenomFallbackOnce = false;
    }
    else
    {
        s_windowsRendererState.gridDebugBasis = WindowsRendererGridDebugBasis::Client;
        s_windowsRendererState.gridDebugDenomPhysW = (std::max)(1u, cw);
        if (!s_loggedGridClientDenomFallbackOnce)
        {
            s_loggedGridClientDenomFallbackOnce = true;
            wchar_t fb[224] = {};
            swprintf_s(
                fb,
                _countof(fb),
                L"[GRID] fallback denom=client cw=%u (no committed grid snapshot yet)\r\n",
                static_cast<unsigned int>(cw));
            OutputDebugStringW(fb);
        }
    }
}

static bool Win32_T16_GetDpiForWindowBest(HWND hwnd, UINT& outDpiX, UINT& outDpiY)
{
    if (hwnd && IsWindow(hwnd))
    {
        const UINT dpi = GetDpiForWindow(hwnd);
        if (dpi != 0)
        {
            outDpiX = dpi;
            outDpiY = dpi;
            return true;
        }
    }
    if (!s_displayMonitorsCache.empty() && kT14SelectedMonitorIndex < s_displayMonitorsCache.size())
    {
        const HMONITOR hmSel = s_displayMonitorsCache[kT14SelectedMonitorIndex].hMonitor;
        if (hmSel)
        {
            UINT dx = 0;
            UINT dy = 0;
            if (GetDpiForMonitor(hmSel, MDT_EFFECTIVE_DPI, &dx, &dy) == S_OK)
            {
                outDpiX = dx;
                outDpiY = dy;
                return true;
            }
        }
    }
    HMONITOR hm = MonitorFromWindow(hwnd ? hwnd : nullptr, MONITOR_DEFAULTTOPRIMARY);
    if (hm)
    {
        UINT dx = 0;
        UINT dy = 0;
        if (GetDpiForMonitor(hm, MDT_EFFECTIVE_DPI, &dx, &dy) == S_OK)
        {
            outDpiX = dx;
            outDpiY = dy;
            return true;
        }
    }
    HDC hdc = GetDC(nullptr);
    outDpiX = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
    outDpiY = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSY));
    ReleaseDC(nullptr, hdc);
    return true;
}

static HMONITOR Win32_T16_GetSizingMonitor(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
    {
        return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    }
    if (!s_displayMonitorsCache.empty() && kT14SelectedMonitorIndex < s_displayMonitorsCache.size())
    {
        const HMONITOR hm = s_displayMonitorsCache[kT14SelectedMonitorIndex].hMonitor;
        if (hm)
        {
            return hm;
        }
    }
    return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

static void Win32_T16_ClampLogicalClientToWorkArea(
    HWND hwnd,
    UINT dpiX,
    UINT dpiY,
    int& inOutLogicalW,
    int& inOutLogicalH)
{
    const HMONITOR hMon = Win32_T16_GetSizingMonitor(hwnd);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hMon, &mi))
    {
        return;
    }
    const int workPhysW = mi.rcWork.right - mi.rcWork.left;
    const int workPhysH = mi.rcWork.bottom - mi.rcWork.top;

    RECT tryRc{};
    tryRc.left = 0;
    tryRc.top = 0;
    tryRc.right = inOutLogicalW;
    tryRc.bottom = inOutLogicalH;
    if (!AdjustWindowRectExForDpi(
            &tryRc,
            WS_OVERLAPPEDWINDOW | WS_VSCROLL,
            TRUE,
            0,
            dpiX))
    {
        return;
    }
    const int outerLogicalW = static_cast<int>(tryRc.right - tryRc.left);
    const int outerLogicalH = static_cast<int>(tryRc.bottom - tryRc.top);
    const int outerPhysW = MulDiv(outerLogicalW, dpiX, USER_DEFAULT_SCREEN_DPI);
    const int outerPhysH = MulDiv(outerLogicalH, dpiY, USER_DEFAULT_SCREEN_DPI);

    if (outerPhysW <= workPhysW && outerPhysH <= workPhysH)
    {
        return;
    }
    const double scaleX = static_cast<double>(workPhysW) /
                          static_cast<double>(outerPhysW > 0 ? outerPhysW : 1);
    const double scaleY = static_cast<double>(workPhysH) /
                          static_cast<double>(outerPhysH > 0 ? outerPhysH : 1);
    const double scale = (std::min)(scaleX, scaleY);
    if (scale >= 1.0)
    {
        return;
    }
    inOutLogicalW = static_cast<int>(static_cast<double>(inOutLogicalW) * scale);
    inOutLogicalH = static_cast<int>(static_cast<double>(inOutLogicalH) * scale);
    if (inOutLogicalW < 64)
    {
        inOutLogicalW = 64;
    }
    if (inOutLogicalH < 64)
    {
        inOutLogicalH = 64;
    }
}

static bool Win32_BuildWindowConfigFromCurrentSelection(HWND hwnd, MainWindowConfig& out)
{
    int physW = 0;
    int physH = 0;
    if (!Win32_T16_ResolveWindowedTargetPhysicalSize(physW, physH, true))
    {
        return false;
    }
    if (physW <= 0 || physH <= 0)
    {
        return false;
    }
    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    Win32_T16_GetDpiForWindowBest(hwnd, dpiX, dpiY);

    int lw = MulDiv(physW, USER_DEFAULT_SCREEN_DPI, dpiX);
    int lh = MulDiv(physH, USER_DEFAULT_SCREEN_DPI, dpiY);
    Win32_T16_ClampLogicalClientToWorkArea(hwnd, dpiX, dpiY, lw, lh);

    out.clientWidth = lw;
    out.clientHeight = lh;
    out.windowStyle = WS_OVERLAPPEDWINDOW | WS_VSCROLL;
    out.windowExStyle = 0;
    out.useAdjustWindowRect = TRUE;
    out.dpiX = dpiX;
    out.dpiY = dpiY;
    out.fillMonitorPhysical = FALSE;
    out.createPhysicalX = 0;
    out.createPhysicalY = 0;
    out.createPhysicalW = 0;
    out.createPhysicalH = 0;

    s_t16LastTargetPhysicalW = physW;
    s_t16LastTargetPhysicalH = physH;
    s_t16LastTargetLogicalW = lw;
    s_t16LastTargetLogicalH = lh;
    s_t16LastDpiX = dpiX;
    s_t16LastDpiY = dpiY;

    return true;
}

static bool Win32_T16_ComputeWindowedOuterPhysicalPixels(
    const MainWindowConfig& cfg,
    int& outOuterPhysW,
    int& outOuterPhysH)
{
    if (cfg.fillMonitorPhysical)
    {
        return false;
    }
    if (cfg.clientWidth <= 0 || cfg.clientHeight <= 0)
    {
        return false;
    }
    RECT rc{};
    rc.left = 0;
    rc.top = 0;
    rc.right = cfg.clientWidth;
    rc.bottom = cfg.clientHeight;
    outOuterPhysW = cfg.clientWidth;
    outOuterPhysH = cfg.clientHeight;
    if (cfg.useAdjustWindowRect)
    {
        if (!AdjustWindowRectExForDpi(
                &rc, cfg.windowStyle, TRUE, cfg.windowExStyle, cfg.dpiX))
        {
            return false;
        }
        const int outerLogicalW = static_cast<int>(rc.right - rc.left);
        const int outerLogicalH = static_cast<int>(rc.bottom - rc.top);
        outOuterPhysW = MulDiv(outerLogicalW, cfg.dpiX, USER_DEFAULT_SCREEN_DPI);
        outOuterPhysH = MulDiv(outerLogicalH, cfg.dpiY, USER_DEFAULT_SCREEN_DPI);
    }
    return true;
}

static void Win32_T16_SetWindowedOuterFrameAtPos(
    HWND hwnd,
    int posX,
    int posY,
    int outerPhysW,
    int outerPhysH,
    bool showNormal)
{
    if (!hwnd || outerPhysW <= 0 || outerPhysH <= 0)
    {
        return;
    }
    SetWindowPos(
        hwnd,
        nullptr,
        posX,
        posY,
        outerPhysW,
        outerPhysH,
        SWP_NOZORDER | SWP_FRAMECHANGED);
    if (showNormal)
    {
        ShowWindow(hwnd, SW_SHOWNORMAL);
    }
    if (IsZoomed(hwnd))
    {
        ShowWindow(hwnd, SW_RESTORE);
        SetWindowPos(
            hwnd,
            nullptr,
            posX,
            posY,
            outerPhysW,
            outerPhysH,
            SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    UpdateWindow(hwnd);
}

static void Win32_T16_LogAndStoreActualMetricsAfterCreate(
    HWND hwnd,
    int requestedOuterPhysW,
    int requestedOuterPhysH,
    const wchar_t* afterLabel)
{
    if (!hwnd)
    {
        return;
    }
    s_t16LastRequestedOuterW = requestedOuterPhysW;
    s_t16LastRequestedOuterH = requestedOuterPhysH;

    RECT outerActual{};
    GetWindowRect(hwnd, &outerActual);
    s_t16LastActualOuterW = outerActual.right - outerActual.left;
    s_t16LastActualOuterH = outerActual.bottom - outerActual.top;

    RECT cr{};
    GetClientRect(hwnd, &cr);
    const int actualClientW = static_cast<int>(cr.right - cr.left);
    const int actualClientH = static_cast<int>(cr.bottom - cr.top);

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    UINT showCmd = 0;
    if (GetWindowPlacement(hwnd, &wp))
    {
        showCmd = wp.showCmd;
    }

    wchar_t logAfter[512] = {};
    swprintf_s(
        logAfter,
        _countof(logAfter),
        L"[T16] %s AFTER: ok=1 client(phys)=%dx%d requested outer(phys)=%dx%d "
        L"actual outer(phys)=%dx%d IsZoomed=%d showCmd=%u hwnd=%p\r\n",
        afterLabel,
        actualClientW,
        actualClientH,
        requestedOuterPhysW,
        requestedOuterPhysH,
        s_t16LastActualOuterW,
        s_t16LastActualOuterH,
        IsZoomed(hwnd) ? 1 : 0,
        showCmd,
        static_cast<void*>(hwnd));
    OutputDebugStringW(logAfter);
}

static bool Win32_RecreateMainWindowFromConfig(HWND oldHwnd, const MainWindowConfig& cfg)
{
    if (!oldHwnd || !IsWindow(oldHwnd))
    {
        return false;
    }
    if (cfg.fillMonitorPhysical)
    {
        if (cfg.createPhysicalW <= 0 || cfg.createPhysicalH <= 0)
        {
            return false;
        }
    }
    else if (cfg.clientWidth <= 0 || cfg.clientHeight <= 0)
    {
        return false;
    }

    int outerPhysW = 0;
    int outerPhysH = 0;
    int posX = 0;
    int posY = 0;

    if (cfg.fillMonitorPhysical)
    {
        outerPhysW = cfg.createPhysicalW;
        outerPhysH = cfg.createPhysicalH;
        posX = cfg.createPhysicalX;
        posY = cfg.createPhysicalY;
    }
    else
    {
        if (!Win32_T16_ComputeWindowedOuterPhysicalPixels(cfg, outerPhysW, outerPhysH))
        {
            s_t16RecreateStatus = T16RecreateStatus::Fail;
            OutputDebugStringW(L"[T16] AdjustWindowRectExForDpi failed\r\n");
            return false;
        }
        RECT wr{};
        GetWindowRect(oldHwnd, &wr);
        posX = static_cast<int>(wr.left);
        posY = static_cast<int>(wr.top);
    }

    wchar_t logBefore[512] = {};
    if (cfg.fillMonitorPhysical)
    {
        swprintf_s(
            logBefore,
            _countof(logBefore),
            L"[T16] recreate BEFORE: fillMonitor physical outer=%dx%d pos=(%d,%d) "
            L"style=0x%08X exStyle=0x%08X\r\n",
            outerPhysW,
            outerPhysH,
            posX,
            posY,
            cfg.windowStyle,
            cfg.windowExStyle);
    }
    else
    {
        swprintf_s(
            logBefore,
            _countof(logBefore),
            L"[T16] recreate BEFORE: mode physical=%dx%d logical client=%dx%d dpi=%u/%u "
            L"outer physical=%dx%d style=0x%08X exStyle=0x%08X topleft=(%d,%d)\r\n",
            s_t16LastTargetPhysicalW,
            s_t16LastTargetPhysicalH,
            cfg.clientWidth,
            cfg.clientHeight,
            cfg.dpiX,
            cfg.dpiY,
            outerPhysW,
            outerPhysH,
            cfg.windowStyle,
            cfg.windowExStyle,
            posX,
            posY);
    }
    OutputDebugStringW(logBefore);

    HWND newHwnd = CreateWindowExW(
        cfg.windowExStyle,
        szWindowClass,
        szTitle,
        cfg.windowStyle,
        posX,
        posY,
        outerPhysW,
        outerPhysH,
        nullptr,
        nullptr,
        hInst,
        nullptr);

    if (!newHwnd)
    {
        s_t16RecreateStatus = T16RecreateStatus::Fail;
        wchar_t logErr[160] = {};
        swprintf_s(
            logErr,
            _countof(logErr),
            L"[T16] CreateWindowEx failed gle=%lu\r\n",
            static_cast<unsigned long>(GetLastError()));
        OutputDebugStringW(logErr);
        return false;
    }

    s_mainWindowHwnd = newHwnd;

    {
        RECT cr{};
        GetClientRect(newHwnd, &cr);
        WindowsRendererConfig wrCfg{};
        wrCfg.clientWidth = static_cast<UINT32>(
            (std::max)(0, static_cast<int>(cr.right - cr.left)));
        wrCfg.clientHeight = static_cast<UINT32>(
            (std::max)(0, static_cast<int>(cr.bottom - cr.top)));
        WindowsRenderer_Shutdown(&s_windowsRendererState);
        (void)WindowsRenderer_Init(newHwnd, wrCfg, &s_windowsRendererState);
    }

    if (!Win32_RegisterKeyboardRawInput(newHwnd))
    {
        OutputDebugStringW(L"[T16] RegisterRawInputDevices failed\r\n");
    }
    SetTimer(newHwnd, TIMER_ID_XINPUT_POLL, XINPUT_POLL_INTERVAL_MS, nullptr);

    if (cfg.fillMonitorPhysical)
    {
        SetMenu(newHwnd, nullptr);
    }

    s_t16DestroyIsRecreate = true;
    DestroyWindow(oldHwnd);

    // CreateWindow 直後の枠・既定状態のずれや SW_SHOW 系の影響を避け、通常ウィンドウとして outer を明示固定する。
    Win32_T16_SetWindowedOuterFrameAtPos(newHwnd, posX, posY, outerPhysW, outerPhysH, true);
    SetForegroundWindow(newHwnd);

    Win32_T16_LogAndStoreActualMetricsAfterCreate(newHwnd, outerPhysW, outerPhysH, L"recreate");

    s_t16RecreateStatus = T16RecreateStatus::Ok;
    return true;
}

// T14/T17 の現在選択に合わせてウィンドウを作り直す。実際の枠・CDS は Win32_T17_ApplyCurrentPresentationMode 内。
static void Win32_T16_RecreateMainWindowFromCurrentSelection(HWND oldHwnd)
{
    Win32_T17_ApplyCurrentPresentationMode(oldHwnd);
}

// ---------------------------------------------------------------------------
// T17: プレゼンテーション（Windowed / Borderless / Fullscreen）
//
// 方針（T16 再生成・高解像度モニタ含む）。モード別の正式方針（window/client・swapchain・offscreen・Present・GDI）は docs/t35_display_mode_policy.md（T35 固定）。T34 committed は T17 の targetPhys/client/outer とは別軸。
// - Windowed: T14 選択モード（なければ T15 最近傍）をクライアント物理サイズに解決し、AdjustWindowRectExForDpi で外枠。
//   fillMonitorPhysical は使わない。
// - Borderless: デスクトップ解像度は CDS で変えない。選択モニタの monitor_rect 全面に WS_POPUP（fillMonitorPhysical）。
//   キャッシュ上のモニタ矩形＝現在のデスクトップ作業領域の想定。
// - Fullscreen: ChangeDisplaySettingsEx(..., CDS_FULLSCREEN) で T15 最近傍モードに合わせた後、再生成の外枠は
//   「キャッシュの monitor_rect 全幅」ではなく、CDS と同一の物理ピクセル（×幅×高さ）をモニタ左上に配置する。
//   CDS 失敗時は Borderless と同じ fullMonitor_rect 再生成。
// ---------------------------------------------------------------------------

static const wchar_t* Win32_T17_ModeLabel(T17PresentationMode m)
{
    switch (m)
    {
    case T17PresentationMode::Windowed:
        return L"Windowed";
    case T17PresentationMode::Borderless:
        return L"Borderless";
    case T17PresentationMode::Fullscreen:
        return L"Fullscreen";
    default:
        return L"?";
    }
}

// デスクトップ解像度を戻す。スキップ時は DISP_CHANGE_SUCCESSFUL を返す。
static LONG Win32_T17_ResetMonitor0DisplaySettings()
{
    if (!s_t17FullscreenDisplayAppliedNow || s_displayMonitorsCache.empty())
    {
        return DISP_CHANGE_SUCCESSFUL;
    }
    const std::wstring& dev = s_displayMonitorsCache[0].device_name;
    const LONG r =
        ChangeDisplaySettingsExW(dev.c_str(), nullptr, nullptr, CDS_RESET, nullptr);
    wchar_t line[256] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T17] ChangeDisplaySettingsEx reset (CDS_RESET) result=%ld\r\n",
        static_cast<long>(r));
    OutputDebugStringW(line);
    s_t17FullscreenDisplayAppliedNow = false;
    return r;
}

static LONG Win32_T17_TryFullscreenDisplayNearestMode()
{
    if (s_displayMonitorsCache.empty() || kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        return DISP_CHANGE_BADPARAM;
    }
    if (s_t15MatchResult.nearestModeIndex == static_cast<size_t>(-1))
    {
        return DISP_CHANGE_BADPARAM;
    }
    const std::vector<DisplayModeInfo>& modes =
        s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
    if (s_t15MatchResult.nearestModeIndex >= modes.size())
    {
        return DISP_CHANGE_BADPARAM;
    }
    const DisplayModeInfo& m = modes[s_t15MatchResult.nearestModeIndex];
    const std::wstring& dev = s_displayMonitorsCache[0].device_name;

    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    if (m.refresh_hz > 0)
    {
        dm.dmFields |= DM_DISPLAYFREQUENCY;
    }
    dm.dmPelsWidth = m.width;
    dm.dmPelsHeight = m.height;
    dm.dmBitsPerPel = m.bits_per_pixel;
    if (m.refresh_hz > 0)
    {
        dm.dmDisplayFrequency = m.refresh_hz;
    }

    const LONG r =
        ChangeDisplaySettingsExW(dev.c_str(), &dm, nullptr, CDS_FULLSCREEN, nullptr);
    return r;
}

// outerUsesCdsModeSize: true のとき、CDS で適用したモードと同じ物理サイズで outer を作る（キャッシュ monitor_rect の大きさは使わない）。
static bool Win32_T17_BuildFillMonitorConfig(HWND hwnd, MainWindowConfig& out, bool outerUsesCdsModeSize)
{
    if (s_displayMonitorsCache.empty() || kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        return false;
    }
    int physW = 0;
    int physH = 0;
    if (!Win32_T16_GetTargetClientSizeFromNearestMode(physW, physH))
    {
        return false;
    }
    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    Win32_T16_GetDpiForWindowBest(hwnd, dpiX, dpiY);
    const int lw = MulDiv(physW, USER_DEFAULT_SCREEN_DPI, dpiX);
    const int lh = MulDiv(physH, USER_DEFAULT_SCREEN_DPI, dpiY);
    s_t16LastTargetPhysicalW = physW;
    s_t16LastTargetPhysicalH = physH;
    s_t16LastTargetLogicalW = lw;
    s_t16LastTargetLogicalH = lh;
    s_t16LastDpiX = dpiX;
    s_t16LastDpiY = dpiY;

    const RECT& mr = s_displayMonitorsCache[kT14SelectedMonitorIndex].monitor_rect;
    out.windowStyle = WS_POPUP | WS_VSCROLL;
    out.windowExStyle = WS_EX_APPWINDOW;
    out.useAdjustWindowRect = FALSE;
    out.dpiX = dpiX;
    out.dpiY = dpiY;
    out.fillMonitorPhysical = TRUE;
    out.createPhysicalX = static_cast<int>(mr.left);
    out.createPhysicalY = static_cast<int>(mr.top);
    if (outerUsesCdsModeSize)
    {
        out.clientWidth = physW;
        out.clientHeight = physH;
        out.createPhysicalW = physW;
        out.createPhysicalH = physH;
        wchar_t line[224] = {};
        swprintf_s(
            line,
            _countof(line),
            L"[T17] fill config: outer=cdsModePhys %dx%d at (%d,%d) (not cached monitor_rect size)\r\n",
            physW,
            physH,
            out.createPhysicalX,
            out.createPhysicalY);
        OutputDebugStringW(line);
    }
    else
    {
        out.clientWidth = static_cast<int>(mr.right - mr.left);
        out.clientHeight = static_cast<int>(mr.bottom - mr.top);
        out.createPhysicalW = out.clientWidth;
        out.createPhysicalH = out.clientHeight;
        wchar_t line[224] = {};
        swprintf_s(
            line,
            _countof(line),
            L"[T17] fill config: outer=fullMonitor cached rect %dx%d at (%d,%d)\r\n",
            out.createPhysicalW,
            out.createPhysicalH,
            out.createPhysicalX,
            out.createPhysicalY);
        OutputDebugStringW(line);
    }
    return true;
}

static void Win32_T17_LogStateVisibleMode(HWND hwnd, T17PresentationMode visibleMode, int cdsApplied01)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        return;
    }
    const LONG style = static_cast<LONG>(GetWindowLongW(hwnd, GWL_STYLE));
    const LONG exStyle = static_cast<LONG>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    const int ow = static_cast<int>(wr.right - wr.left);
    const int oh = static_cast<int>(wr.bottom - wr.top);
    RECT cr{};
    GetClientRect(hwnd, &cr);
    const int cw = static_cast<int>(cr.right - cr.left);
    const int ch = static_cast<int>(cr.bottom - cr.top);
    wchar_t line[448] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T17] STATE visibleMode=%s cdsApplied=%d outerPhys=%dx%d clientPhys=%dx%d "
        L"style=0x%08lX exStyle=0x%08lX\r\n",
        Win32_T17_ModeLabel(visibleMode),
        cdsApplied01,
        ow,
        oh,
        cw,
        ch,
        static_cast<unsigned long>(style),
        static_cast<unsigned long>(exStyle));
    OutputDebugStringW(line);
}

static void Win32_T17_CyclePresentationMode(HWND hwnd)
{
    switch (s_t17CurrentPresentationMode)
    {
    case T17PresentationMode::Windowed:
        s_t17CurrentPresentationMode = T17PresentationMode::Borderless;
        break;
    case T17PresentationMode::Borderless:
        s_t17CurrentPresentationMode = T17PresentationMode::Fullscreen;
        break;
    case T17PresentationMode::Fullscreen:
        s_t17CurrentPresentationMode = T17PresentationMode::Windowed;
        break;
    }
    wchar_t line[192] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T17] mode cycle seq=%u key=F6 -> candidate=%s\r\n",
        s_t17CycleSeq,
        Win32_T17_ModeLabel(s_t17CurrentPresentationMode));
    OutputDebugStringW(line);
    if (hwnd)
    {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// F6 で選んだ候補を実際の枠・CDS・再生成に反映する。Enter の短タップは WM_INPUT でラッチしタイマーで 1 回消費。
static void Win32_T17_ApplyCurrentPresentationMode(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }

    ++s_t17ApplySeq;
    const T17PresentationMode candidate = s_t17CurrentPresentationMode;
    const T17PresentationMode lastAppliedBefore = s_t17LastAppliedPresentationMode;

    wchar_t begin[320] = {};
    swprintf_s(
        begin,
        _countof(begin),
        L"[T17] APPLY BEGIN seq=%u candidate=%s lastApplied=%s\r\n",
        s_t17ApplySeq,
        Win32_T17_ModeLabel(candidate),
        Win32_T17_ModeLabel(lastAppliedBefore));
    OutputDebugStringW(begin);

    bool desktopResetAttempted = false;
    LONG desktopResetResult = DISP_CHANGE_SUCCESSFUL;
    if (s_t17FullscreenDisplayAppliedNow &&
        (candidate == T17PresentationMode::Windowed || candidate == T17PresentationMode::Borderless))
    {
        desktopResetAttempted = true;
        desktopResetResult = Win32_T17_ResetMonitor0DisplaySettings();
        wchar_t resetLog[192] = {};
        swprintf_s(
            resetLog,
            _countof(resetLog),
            L"[T17] APPLY desktop reset (CDS_RESET) result=%ld\r\n",
            static_cast<long>(desktopResetResult));
        OutputDebugStringW(resetLog);
    }

    T17PresentationMode appliedMode = candidate;
    MainWindowConfig cfg{};
    bool built = false;

    if (candidate == T17PresentationMode::Windowed)
    {
        built = Win32_BuildWindowConfigFromCurrentSelection(hwnd, cfg);
    }
    else if (candidate == T17PresentationMode::Borderless)
    {
        built = Win32_T17_BuildFillMonitorConfig(hwnd, cfg, false);
    }
    else
    {
        const LONG r = Win32_T17_TryFullscreenDisplayNearestMode();
        s_t17LastFullscreenChangeResult = r;
        int bpp = 0;
        int hz = 0;
        int mw = 0;
        int mh = 0;
        if (!s_displayMonitorsCache.empty() && kT14SelectedMonitorIndex < s_displayMonitorsCache.size() &&
            s_t15MatchResult.nearestModeIndex != static_cast<size_t>(-1))
        {
            const auto& modes = s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
            if (s_t15MatchResult.nearestModeIndex < modes.size())
            {
                const DisplayModeInfo& m = modes[s_t15MatchResult.nearestModeIndex];
                mw = m.width;
                mh = m.height;
                bpp = m.bits_per_pixel;
                hz = m.refresh_hz;
            }
        }
        wchar_t cdsLog[256] = {};
        swprintf_s(
            cdsLog,
            _countof(cdsLog),
            L"[T17] APPLY CDS result=%ld mode=%dx%d %dbpp %dHz\r\n",
            static_cast<long>(r),
            mw,
            mh,
            bpp,
            hz);
        OutputDebugStringW(cdsLog);

        if (r == DISP_CHANGE_SUCCESSFUL)
        {
            s_t17FullscreenDisplayAppliedNow = true;
            built = Win32_T17_BuildFillMonitorConfig(hwnd, cfg, true);
        }
        else
        {
            OutputDebugStringW(L"[T17] APPLY CDS failed; fallback Borderless (recreate fill monitor, no CDS)\r\n");
            appliedMode = T17PresentationMode::Borderless;
            built = Win32_T17_BuildFillMonitorConfig(hwnd, cfg, false);
        }
    }

    if (!built)
    {
        s_t17LastWindowApplySuccess = false;
        s_t16RecreateStatus = T16RecreateStatus::Fail;
        wcscpy_s(s_t17LastActionLine, _countof(s_t17LastActionLine), L"Apply failed (build config)");
        swprintf_s(
            s_t17LastEnterCandidateToApplied,
            _countof(s_t17LastEnterCandidateToApplied),
            L">>> Enter: %s -> (build failed) <<<",
            Win32_T17_ModeLabel(candidate));
        wchar_t endFail[192] = {};
        swprintf_s(
            endFail,
            _countof(endFail),
            L"[T17] APPLY END seq=%u result=fail reason=build_config\r\n",
            s_t17ApplySeq);
        OutputDebugStringW(endFail);
        return;
    }

    // グリッド用: 再作成で HWND が変わる前に T14 インデックスと modes[] を直接読む（GetSelected 再呼び出しに依存しない）。
    const size_t gridSnapT14Idx = s_t14SelectedModeIndex;
    int gridSnapW = 0;
    int gridSnapH = 0;
    bool gridSnapOk = false;
    if (!s_displayMonitorsCache.empty() && kT14SelectedMonitorIndex < s_displayMonitorsCache.size())
    {
        const std::vector<DisplayModeInfo>& modes =
            s_displayMonitorsCache[kT14SelectedMonitorIndex].modes;
        if (!modes.empty() && gridSnapT14Idx < modes.size())
        {
            gridSnapW = modes[gridSnapT14Idx].width;
            gridSnapH = modes[gridSnapT14Idx].height;
            gridSnapOk = true;
        }
    }

    // ログ順（目安）: [GRID] commit → Win32_RecreateMainWindowFromConfig 内の WM_PAINT で [GRID] mode=...
    // （Refresh はこの時点で committed 済み）→ 成功後 [GRID] redraw requested → RedrawWindow で再度 [GRID] mode=...
    // 再作成失敗時は committed をロールバック。
    const int prevCommittedW = s_lastCommittedGridSelectedPhysW;
    const int prevCommittedH = s_lastCommittedGridSelectedPhysH;
    bool gridCommittedPreRecreate = false;
    if (gridSnapOk)
    {
        s_lastCommittedGridSelectedPhysW = gridSnapW;
        s_lastCommittedGridSelectedPhysH = gridSnapH;
        s_loggedGridClientDenomFallbackOnce = false;
        gridCommittedPreRecreate = true;
        wchar_t gridCommitLine[288] = {};
        swprintf_s(
            gridCommitLine,
            _countof(gridCommitLine),
            L"[GRID] commit t14Idx=%zu t14Mode=%dx%d committedGrid=%dx%d\r\n",
            gridSnapT14Idx,
            gridSnapW,
            gridSnapH,
            gridSnapW,
            gridSnapH);
        OutputDebugStringW(gridCommitLine);
    }

    // T35: 再生成中の WM_PAINT が古い Borderless とみなして T34 を有効化しないよう、再生成前に適用済みモードを更新する
    const T17PresentationMode lastAppliedSnapshot = s_t17LastAppliedPresentationMode;
    s_t17LastAppliedPresentationMode = appliedMode;

    const bool ok = Win32_RecreateMainWindowFromConfig(hwnd, cfg);
    s_t17LastWindowApplySuccess = ok;

    if (!ok)
    {
        s_t17LastAppliedPresentationMode = lastAppliedSnapshot;
        if (gridCommittedPreRecreate)
        {
            s_lastCommittedGridSelectedPhysW = prevCommittedW;
            s_lastCommittedGridSelectedPhysH = prevCommittedH;
            OutputDebugStringW(L"[GRID] commit rolled back: recreate failed\r\n");
        }
    }

    HWND hwndAfter = s_mainWindowHwnd;
    int clientW = 0;
    int clientH = 0;
    if (ok && hwndAfter && IsWindow(hwndAfter))
    {
        RECT cr{};
        GetClientRect(hwndAfter, &cr);
        clientW = static_cast<int>(cr.right - cr.left);
        clientH = static_cast<int>(cr.bottom - cr.top);
    }

    if (ok && hwndAfter && IsWindow(hwndAfter) && gridSnapOk)
    {
        WindowsRenderer_DebugGrid_ResetLogOnce();
        InvalidateRect(hwndAfter, nullptr, FALSE);
        OutputDebugStringW(L"[GRID] redraw requested after commit\r\n");
        RedrawWindow(hwndAfter, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }

    if (ok)
    {
        if (!gridSnapOk)
        {
            wchar_t gridSkipLine[224] = {};
            swprintf_s(
                gridSkipLine,
                _countof(gridSkipLine),
                L"[GRID] commit skip: invalid T14 snapshot idx=%zu applyOk=1\r\n",
                gridSnapT14Idx);
            OutputDebugStringW(gridSkipLine);
        }

        const wchar_t* fillOuterPolicy = L"windowed";
        if (appliedMode == T17PresentationMode::Fullscreen && s_t17FullscreenDisplayAppliedNow)
        {
            fillOuterPolicy = L"cdsMode";
        }
        else if (appliedMode == T17PresentationMode::Borderless)
        {
            fillOuterPolicy = L"fullMonitor";
        }

        wchar_t recLog[448] = {};
        swprintf_s(
            recLog,
            _countof(recLog),
            L"[T17] APPLY RECREATE result=ok candidate=%s applied=%s fillOuter=%s "
            L"targetPhys=%dx%d client=%dx%d outer=%dx%d\r\n",
            Win32_T17_ModeLabel(candidate),
            Win32_T17_ModeLabel(appliedMode),
            fillOuterPolicy,
            s_t16LastTargetPhysicalW,
            s_t16LastTargetPhysicalH,
            clientW,
            clientH,
            s_t16LastActualOuterW,
            s_t16LastActualOuterH);
        OutputDebugStringW(recLog);

        const int cdsNow = s_t17FullscreenDisplayAppliedNow ? 1 : 0;
        Win32_T17_LogStateVisibleMode(hwndAfter, appliedMode, cdsNow);

        if (appliedMode == T17PresentationMode::Windowed)
        {
            if (desktopResetAttempted)
            {
                swprintf_s(
                    s_t17LastActionLine,
                    _countof(s_t17LastActionLine),
                    L"Applied Windowed (desktop reset %s, recreate ok)",
                    (desktopResetResult == DISP_CHANGE_SUCCESSFUL) ? L"ok" : L"fail");
            }
            else
            {
                wcscpy_s(s_t17LastActionLine, _countof(s_t17LastActionLine), L"Applied Windowed (recreate ok)");
            }
        }
        else if (appliedMode == T17PresentationMode::Borderless)
        {
            if (candidate == T17PresentationMode::Fullscreen)
            {
                swprintf_s(
                    s_t17LastActionLine,
                    _countof(s_t17LastActionLine),
                    L"Applied Borderless (CDS failed, recreate ok)");
            }
            else if (desktopResetAttempted)
            {
                swprintf_s(
                    s_t17LastActionLine,
                    _countof(s_t17LastActionLine),
                    L"Applied Borderless (desktop reset %s, CDS no, recreate ok)",
                    (desktopResetResult == DISP_CHANGE_SUCCESSFUL) ? L"ok" : L"fail");
            }
            else
            {
                swprintf_s(
                    s_t17LastActionLine,
                    _countof(s_t17LastActionLine),
                    L"Applied Borderless (CDS no, recreate ok)");
            }
        }
        else
        {
            swprintf_s(
                s_t17LastActionLine,
                _countof(s_t17LastActionLine),
                L"Applied Fullscreen (CDS ok, recreate ok)");
        }

        wchar_t endOk[256] = {};
        swprintf_s(
            endOk,
            _countof(endOk),
            L"[T17] APPLY END seq=%u applied=%s fullscreenDisplayAppliedNow=%d\r\n",
            s_t17ApplySeq,
            Win32_T17_ModeLabel(appliedMode),
            s_t17FullscreenDisplayAppliedNow ? 1 : 0);
        swprintf_s(
            s_t17LastEnterCandidateToApplied,
            _countof(s_t17LastEnterCandidateToApplied),
            L">>> Enter: %s -> %s <<<",
            Win32_T17_ModeLabel(candidate),
            Win32_T17_ModeLabel(appliedMode));
        OutputDebugStringW(endOk);
        Win32DebugOverlay_ScrollLog(
            L"T17 apply (post-recreate; contentH/T17DocY stale until next WM_PAINT)",
            hwndAfter,
            s_paintScrollY,
            s_paintScrollY,
            -1,
            -1,
            -1,
            -1);
    }
    else
    {
        wcscpy_s(s_t17LastActionLine, _countof(s_t17LastActionLine), L"Apply failed (recreate)");
        swprintf_s(
            s_t17LastEnterCandidateToApplied,
            _countof(s_t17LastEnterCandidateToApplied),
            L">>> Enter: %s -> (recreate failed) <<<",
            Win32_T17_ModeLabel(candidate));
        wchar_t recFail[192] = {};
        swprintf_s(
            recFail,
            _countof(recFail),
            L"[T17] APPLY RECREATE result=fail\r\n");
        OutputDebugStringW(recFail);
        wchar_t endFail[192] = {};
        swprintf_s(
            endFail,
            _countof(endFail),
            L"[T17] APPLY END seq=%u result=fail reason=recreate\r\n",
            s_t17ApplySeq);
        OutputDebugStringW(endFail);
    }
}

static const wchar_t* Win32_T16_ShowCmdLabel(UINT sc)
{
    switch (sc)
    {
    case SW_HIDE:
        return L"SW_HIDE";
    case SW_NORMAL: // SW_SHOWNORMAL と同値
        return L"SW_NORMAL";
    case SW_SHOWMINIMIZED:
        return L"SW_SHOWMINIMIZED";
    case SW_SHOWMAXIMIZED:
        return L"SW_SHOWMAXIMIZED";
    case SW_SHOWNOACTIVATE:
        return L"SW_SHOWNOACTIVATE";
    case SW_SHOW:
        return L"SW_SHOW";
    case SW_MINIMIZE:
        return L"SW_MINIMIZE";
    case SW_SHOWMINNOACTIVE:
        return L"SW_SHOWMINNOACTIVE";
    case SW_SHOWNA:
        return L"SW_SHOWNA";
    case SW_RESTORE:
        return L"SW_RESTORE";
    default:
        return L"?";
    }
}

// WM_PAINT 用テキスト: T16（ウィンドウド時の目標／実測・再生成結果）を buf に追記。
static void Win32_T16_AppendPaintSection(
    wchar_t* buf,
    size_t bufCount,
    HWND hwnd,
    const RECT& rcClient)
{
    // Per-Monitor DPI Aware V2: GetClientRect はクライアント領域を物理ピクセルで返す。
    // target の logical（MulDiv(mode,96,dpi)）と比較するには current も同じ式で論理化する。
    const int cw = static_cast<int>(rcClient.right - rcClient.left);
    const int ch = static_cast<int>(rcClient.bottom - rcClient.top);
    int physW = 0;
    int physH = 0;
    const bool hasTarget = Win32_T16_ResolveWindowedTargetPhysicalSize(physW, physH, false);
    const wchar_t* modeSrcLabel = L"-";
    wchar_t t14IdxStr[40] = L"-";
    wchar_t selectedModeLine[96] = L"-";
    int tgtBpp = 0;
    int tgtHz = 0;
    if (hasTarget)
    {
        modeSrcLabel = Win32_T16_WindowedModeSourceLabel(s_t16WindowedTargetModeSource);
        if (s_t16WindowedTargetModeSource == T16WindowedTargetModeSource::T14SelectedList)
        {
            swprintf_s(t14IdxStr, _countof(t14IdxStr), L"%zu", s_t16WindowedTargetT14ListIndex);
            swprintf_s(
                selectedModeLine,
                _countof(selectedModeLine),
                L"%dx%d bpp=%d hz=%d",
                s_t16WindowedTargetMode.width,
                s_t16WindowedTargetMode.height,
                s_t16WindowedTargetMode.bits_per_pixel,
                s_t16WindowedTargetMode.refresh_hz);
        }
        else
        {
            wcscpy_s(t14IdxStr, L"-");
            swprintf_s(
                selectedModeLine,
                _countof(selectedModeLine),
                L"(N/A — T15 nearest idx=%zu)",
                s_t15MatchResult.nearestModeIndex);
        }
        tgtBpp = s_t16WindowedTargetMode.bits_per_pixel;
        tgtHz = s_t16WindowedTargetMode.refresh_hz;
    }
    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    if (hwnd)
    {
        Win32_T16_GetDpiForWindowBest(hwnd, dpiX, dpiY);
    }
    const int curLogicalW = MulDiv(cw, USER_DEFAULT_SCREEN_DPI, dpiX);
    const int curLogicalH = MulDiv(ch, USER_DEFAULT_SCREEN_DPI, dpiY);
    int logicalFromMode = 0;
    int logicalFromModeH = 0;
    if (hasTarget)
    {
        logicalFromMode = MulDiv(physW, USER_DEFAULT_SCREEN_DPI, dpiX);
        logicalFromModeH = MulDiv(physH, USER_DEFAULT_SCREEN_DPI, dpiY);
    }
    int logicalClampedW = logicalFromMode;
    int logicalClampedH = logicalFromModeH;
    if (hasTarget && hwnd)
    {
        logicalClampedW = logicalFromMode;
        logicalClampedH = logicalFromModeH;
        Win32_T16_ClampLogicalClientToWorkArea(hwnd, dpiX, dpiY, logicalClampedW, logicalClampedH);
    }

    const wchar_t* lastLabel = L"(none)";
    if (s_t16RecreateStatus == T16RecreateStatus::Ok)
    {
        lastLabel = L"success";
    }
    else if (s_t16RecreateStatus == T16RecreateStatus::Fail)
    {
        lastLabel = L"fail";
    }
    wchar_t reqOuterStr[48] = L"-";
    wchar_t actOuterStr[48] = L"-";
    if (s_t16RecreateStatus == T16RecreateStatus::Ok)
    {
        swprintf_s(
            reqOuterStr,
            _countof(reqOuterStr),
            L"%dx%d",
            s_t16LastRequestedOuterW,
            s_t16LastRequestedOuterH);
        swprintf_s(
            actOuterStr,
            _countof(actOuterStr),
            L"%dx%d",
            s_t16LastActualOuterW,
            s_t16LastActualOuterH);
    }

    int nowOuterW = 0;
    int nowOuterH = 0;
    BOOL nowZoomed = FALSE;
    UINT nowShowCmd = 0;
    const wchar_t* nowShowCmdLabel = L"-";
    if (hwnd && IsWindow(hwnd))
    {
        RECT wrNow{};
        GetWindowRect(hwnd, &wrNow);
        nowOuterW = static_cast<int>(wrNow.right - wrNow.left);
        nowOuterH = static_cast<int>(wrNow.bottom - wrNow.top);
        nowZoomed = IsZoomed(hwnd);
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(hwnd, &wp))
        {
            nowShowCmd = wp.showCmd;
            nowShowCmdLabel = Win32_T16_ShowCmdLabel(wp.showCmd);
        }
    }

    wchar_t block[2560] = {};
    if (hasTarget)
    {
        swprintf_s(
            block,
            _countof(block),
            L"\r\n--- T16 window (windowed) ---\r\n"
            L"mode source: %s\r\n"
            L"T14 list index: %s\r\n"
            L"selected mode (T14 list): %s\r\n"
            L"target mode (resolved): %dx%d bpp=%d hz=%d\r\n"
            L"current client (raw GetClientRect): %dx%d\r\n"
            L"current client (physical, PMv2 == raw): %dx%d\r\n"
            L"current client (logical, MulDiv(phys,96,dpi)): %dx%d\r\n"
            L"current outer (GetWindowRect, physical): %dx%d\r\n"
            L"IsZoomed: %d  WINDOWPLACEMENT.showCmd: %u (%s)\r\n"
            L"target client (logical, from DPI): %dx%d\r\n"
            L"target client (logical, clamped to work): %dx%d\r\n"
            L"effective DPI: %u x %u\r\n"
            L"last recreate: requested outer (physical): %s\r\n"
            L"last recreate: actual outer (physical, after SetWindowPos): %s\r\n"
            L"last recreate result: %s\r\n"
            L"(menu closed: Enter = recreate; T14 list preferred)\r\n",
            modeSrcLabel,
            t14IdxStr,
            selectedModeLine,
            physW,
            physH,
            tgtBpp,
            tgtHz,
            cw,
            ch,
            cw,
            ch,
            curLogicalW,
            curLogicalH,
            nowOuterW,
            nowOuterH,
            nowZoomed ? 1 : 0,
            nowShowCmd,
            nowShowCmdLabel,
            logicalFromMode,
            logicalFromModeH,
            logicalClampedW,
            logicalClampedH,
            dpiX,
            dpiY,
            reqOuterStr,
            actOuterStr,
            lastLabel);
    }
    else
    {
        swprintf_s(
            block,
            _countof(block),
            L"\r\n--- T16 window (windowed) ---\r\n"
            L"mode source: (none)\r\n"
            L"T14 list index: -\r\n"
            L"selected mode (T14 list): -\r\n"
            L"target mode (resolved): (none)\r\n"
            L"current client (raw GetClientRect): %dx%d\r\n"
            L"current client (physical, PMv2 == raw): %dx%d\r\n"
            L"current client (logical, MulDiv(phys,96,dpi)): %dx%d\r\n"
            L"current outer (GetWindowRect, physical): %dx%d\r\n"
            L"IsZoomed: %d  WINDOWPLACEMENT.showCmd: %u (%s)\r\n"
            L"target client (logical, from DPI): -\r\n"
            L"target client (logical, clamped to work): -\r\n"
            L"effective DPI: %u x %u\r\n"
            L"last recreate: requested outer (physical): %s\r\n"
            L"last recreate: actual outer (physical, after SetWindowPos): %s\r\n"
            L"last recreate result: %s\r\n"
            L"(menu closed: Enter = recreate; T14 list preferred)\r\n",
            cw,
            ch,
            cw,
            ch,
            curLogicalW,
            curLogicalH,
            nowOuterW,
            nowOuterH,
            nowZoomed ? 1 : 0,
            nowShowCmd,
            nowShowCmdLabel,
            dpiX,
            dpiY,
            reqOuterStr,
            actOuterStr,
            lastLabel);
    }
    wcscat_s(buf, bufCount, block);
}

static void Win32_T18_TruncateWideForPaint(const wchar_t* src, wchar_t* dst, size_t dstCount, size_t maxLen)
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

// 製品名がデバイスパスに見えるときは T18 表示で path と混同しないよう弾く。
static bool Win32_T18_RawInputProductLooksLikeDevicePath(const wchar_t* s)
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

// ---------------------------------------------------------------------------
// T18: デバッグ表示ブロック（先頭 HID + 先頭 XInput スロット・VID/PID・parser/support）
// ---------------------------------------------------------------------------

static void Win32_T18_AppendPaintSection(wchar_t* buf, size_t bufCount)
{
    wchar_t pathDisp[384] = {};
    wchar_t prodDisp[384] = {};
    Win32_T18_TruncateWideForPaint(s_t18.device_path, pathDisp, _countof(pathDisp), 120);
    Win32_T18_TruncateWideForPaint(
        (s_t18.product_name[0] != L'\0') ? s_t18.product_name : L"",
        prodDisp,
        _countof(prodDisp),
        64);

    wchar_t slotStr[16] = L"-1";
    if (s_t18.xinput_slot >= 0)
    {
        swprintf_s(slotStr, _countof(slotStr), L"%d", s_t18.xinput_slot);
    }

    const unsigned vid = s_t18.hid_found ? static_cast<unsigned>(s_t18.hid.vendor_id) : 0u;
    const unsigned pid = s_t18.hid_found ? static_cast<unsigned>(s_t18.hid.product_id) : 0u;

    wchar_t vidPidLine[128] = {};
    if (s_t18.hid_found)
    {
        swprintf_s(vidPidLine, _countof(vidPidLine), L"vid: 0x%04X  pid: 0x%04X", vid, pid);
    }
    else
    {
        wcscpy_s(vidPidLine, L"vid/pid: n/a (no HID gamepad in Raw Input order)");
    }

    wchar_t block[2048] = {};
    swprintf_s(
        block,
        _countof(block),
        L"\r\n--- T18 controller identify ---\r\n"
        L"slot: %s  (first connected XInput slot; -1 if none)\r\n"
        L"raw HID: %s\r\n"
        L"%s\r\n"
        L"inferred family: %s\r\n"
        L"parser: %s  support: %s\r\n"
        L"product name: %s\r\n"
        L"device path: %s\r\n"
        L"(T18: one device — first HID gamepad in Raw Input enum order; VID/PID table is a receiver bucket; GenericHid+tentative is not a verified button map.)\r\n",
        slotStr,
        s_t18.hid_found ? L"found" : L"not found",
        vidPidLine,
        Win32_GameControllerKindFamilyLabel(s_t18.inferred_kind),
        Win32_ControllerParserKindLabel(s_t18.parser_kind),
        Win32_ControllerSupportLevelLabel(s_t18.support_level),
        prodDisp,
        pathDisp);
    wcscat_s(buf, bufCount, block);
}

// WM_PAINT 用: T17 の候補／適用結果・F5 ヒントなどを buf に追記。
static void Win32_T17_AppendPaintSection(wchar_t* buf, size_t bufCount)
{
    wchar_t t17[2560] = {};
    const wchar_t* f5Line =
        (s_t17F5UnrelatedHint[0] != L'\0')
            ? s_t17F5UnrelatedHint
            : L"F5 is not used by T17 (only F6 cycles, Enter applies). Press F5 for a one-line reminder.";
    swprintf_s(
        t17,
        _countof(t17),
        L"\r\n--- T17 presentation ---\r\n"
        L"mode cycle key: F6\r\n"
        L"apply key: Enter\r\n"
        L"last key affecting T17: %s\r\n"
        L"cycle seq (F6 only): %u\r\n"
        L"apply seq (Enter only): %u\r\n"
        L"last Enter apply (candidate -> applied): %s\r\n"
        L"last action: %s\r\n"
        L"mode (candidate, F6): %s\r\n"
        L"applied mode (actual): %s\r\n"
        L"last apply success: %d\r\n"
        L"CDS applied (desktop mode): %s\r\n"
        L"fullscreen display applied now: %d\r\n"
        L"last ChangeDisplaySettings result (fullscreen try): %ld\r\n"
        L"F5 / T17: %s\r\n"
        L"(menu closed: F6=cycle candidate, Enter=apply)\r\n",
        s_t17LastKeyAffectingT17,
        s_t17CycleSeq,
        s_t17ApplySeq,
        s_t17LastEnterCandidateToApplied,
        s_t17LastActionLine,
        Win32_T17_ModeLabel(s_t17CurrentPresentationMode),
        Win32_T17_ModeLabel(s_t17LastAppliedPresentationMode),
        s_t17LastWindowApplySuccess ? 1 : 0,
        s_t17FullscreenDisplayAppliedNow ? L"yes" : L"no",
        s_t17FullscreenDisplayAppliedNow ? 1 : 0,
        static_cast<long>(s_t17LastFullscreenChangeResult),
        f5Line);
    wcscat_s(buf, bufCount, t17);
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

   Win32_DisplayEnumerateAndCache();
   Win32_T14_ClampIndicesAfterEnumerate();
   Win32_T15_ApplyDesiredPresetAndRecompute();
   Win32_T15_LogDesiredNearestLine();

   int createW = CW_USEDEFAULT;
   int createH = 0;
   MainWindowConfig initCfg{};
   bool useNearestForCreate = false;
   int outerPhysWInit = 0;
   int outerPhysHInit = 0;

   if (Win32_BuildWindowConfigFromCurrentSelection(nullptr, initCfg))
   {
      if (Win32_T16_ComputeWindowedOuterPhysicalPixels(initCfg, outerPhysWInit, outerPhysHInit))
      {
         useNearestForCreate = true;
         createW = outerPhysWInit;
         createH = outerPhysHInit;
         wchar_t logBefore[512] = {};
         const size_t ni = s_t15MatchResult.nearestModeIndex;
         swprintf_s(
             logBefore,
             _countof(logBefore),
             L"[T16] initial CreateWindow BEFORE: nearestMode idx=%zu target mode phys=%dx%d "
             L"logical client=%dx%d dpi=%u/%u outer physical=%dx%d style=0x%08lX exStyle=0x%08lX\r\n",
             ni,
             s_t16LastTargetPhysicalW,
             s_t16LastTargetPhysicalH,
             initCfg.clientWidth,
             initCfg.clientHeight,
             initCfg.dpiX,
             initCfg.dpiY,
             outerPhysWInit,
             outerPhysHInit,
             static_cast<unsigned long>(initCfg.windowStyle),
             static_cast<unsigned long>(initCfg.windowExStyle));
         OutputDebugStringW(logBefore);
      }
      else
      {
         OutputDebugStringW(
             L"[T16] initial CreateWindow: outer physical compute failed; using system default "
             L"(CW_USEDEFAULT)\r\n");
      }
   }
   else
   {
      OutputDebugStringW(
          L"[T16] initial CreateWindow: nearest-mode size unavailable; using system default "
          L"(CW_USEDEFAULT)\r\n");
   }

   HWND hWnd = CreateWindowW(
       szWindowClass,
       szTitle,
       WS_OVERLAPPEDWINDOW | WS_VSCROLL,
       CW_USEDEFAULT,
       0,
       createW,
       createH,
       nullptr,
       nullptr,
       hInstance,
       nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   s_mainWindowHwnd = hWnd;

   {
      RECT cr{};
      GetClientRect(hWnd, &cr);
      WindowsRendererConfig wrCfg{};
      wrCfg.clientWidth = static_cast<UINT32>(
          (std::max)(0, static_cast<int>(cr.right - cr.left)));
      wrCfg.clientHeight = static_cast<UINT32>(
          (std::max)(0, static_cast<int>(cr.bottom - cr.top)));
      (void)WindowsRenderer_Init(hWnd, wrCfg, &s_windowsRendererState);
   }

   if (useNearestForCreate)
   {
      RECT wr{};
      GetWindowRect(hWnd, &wr);
      Win32_T16_SetWindowedOuterFrameAtPos(
          hWnd,
          static_cast<int>(wr.left),
          static_cast<int>(wr.top),
          outerPhysWInit,
          outerPhysHInit,
          false);
      Win32_T16_LogAndStoreActualMetricsAfterCreate(
          hWnd, outerPhysWInit, outerPhysHInit, L"initial CreateWindow");
      s_t16RecreateStatus = T16RecreateStatus::Ok;
   }

   if (!Win32_RegisterKeyboardRawInput(hWnd))
   {
      OutputDebugStringW(L"RegisterRawInputDevices failed\r\n");
   }

   Win32_LogDisplayMonitors();

   Win32_LogXInputSlotsAtStartup();
   Win32_LogRawInputHidGameControllersClassified();
   GamepadButton_LogLabelTablesAtStartup();

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   SetTimer(hWnd, TIMER_ID_XINPUT_POLL, XINPUT_POLL_INTERVAL_MS, nullptr);

   return TRUE;
}

// === T25 [1] Win32: Raw Input キーボード + HID GamePad 登録（PS4 等は WM_INPUT / RIM_TYPEHID） ===
static BOOL Win32_RegisterKeyboardRawInput(HWND hwnd)
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

// === T25 [8] Win32: XInput スロット列挙・接続確認（先頭接続スロット前提の土台） ===
static void Win32_FillControllerSlotProbe(UINT8 slot, ControllerSlotProbeResult& out)
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
    for (UINT8 i = 0; i < XUSER_MAX_COUNT; ++i)
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
    case GameControllerKind::Nintendo: return L"Nintendo";
    case GameControllerKind::XInputCompatible: return L"XInputCompatible";
    default: return L"Unknown";
    }
}

// T18 / paint: 粗い family（PS4/PS5 は PlayStation にまとめる。XInput 互換は enum 名どおり）
static const wchar_t* Win32_GameControllerKindFamilyLabel(GameControllerKind kind)
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

static void GamepadButton_LogLabelTablesAtStartup()
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
        swprintf_s(header, _countof(header), L"family=%s\r\n", Win32_GameControllerKindLabel(family));
        OutputDebugStringW(header);

        const auto count = static_cast<UINT8>(GamepadButtonId::Count);
        for (UINT8 i = 0; i < count; ++i)
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

// PS4 (Sony HID) 調査: Raw Input で届くレポートの差分ログ + VirtualInput 最小橋渡し（XInput 非経路）
static BYTE s_ps4InvestPrevReport[128]{};
static UINT s_ps4InvestPrevReportLen = 0;
static bool s_ps4InvestHasPrev = false;
static BYTE s_ps4PrevBytes0to7[8]{};
static bool s_ps4HasPrevBytes0to7 = false;
static BYTE s_ps4PrevBtn567[3]{}; // レポート差分はボタン領域（主に 5..7）のみ
static bool s_ps4HasPrevBtn567 = false;
static BYTE s_ps4PrevB5to8[4]{};
static bool s_ps4HasPrevB5to8 = false;
static VirtualInputSnapshot s_ps4HidVirtualFromLastReport{};
static bool s_ps4HidVirtualFromLastReportValid = false;
// 最終 DS4 レポート要約（slot=99 bridge delta 用）。WM_INPUT で parseOk 時のみ更新。
static BYTE s_ps4LastReportB5 = 0;
static BYTE s_ps4LastReportB6 = 0;
static BYTE s_ps4LastReportB8 = 0;
static BYTE s_ps4LastReportB9 = 0;
static bool s_ps4LastReportB5to9Valid = false;
static bool s_ps4BridgeDeltaHasPrev = false;
static BYTE s_ps4BridgeDeltaPrevB5 = 0;
static BYTE s_ps4BridgeDeltaPrevB6 = 0;
static BYTE s_ps4BridgeDeltaPrevB8 = 0;
static BYTE s_ps4BridgeDeltaPrevB9 = 0;
// WM_INPUT 経路の冗長ログ（PS4BTN/MAP/CIRCLE/HID/isolate）。既定 false。ビット調査時のみ true。
static constexpr bool kPs4HidVerboseRawLog = false;
// 一時調査: [PS4ISOskip] / [PS4BridgeReset] のみ。既定 false（通常時ログ量は増えない）
// T18: [PS4ISOskip] がコンボで出ないのは skip のバグではなく、WM_INPUT 生が hatLo=8（中立）のフレームでは条件を満たさないため。README「DPad 混在時」参照。
static constexpr bool kPs4IsoSkipDebugLog = false;
// T18: L1/R3/L3/Tri の candidate エッジで hat 分岐の直前に [PS4ISOprobe] 1 行。既定 false。
static constexpr bool kPs4IsoProbeDebugLog = false;
// T18: byte5 下位ニブルまたは VirtualInput の dpad ビット変化時のみ [PS4DPadProbe] 1 行。既定 false。
static constexpr bool kPs4DPadProbeDebugLog = false;
// T18: WM_INPUT 生レポートで hatLo または L1 ビット変化時のみ [PS4RawCombo] 1 行。既定 false。結論は README「DPad 混在時」。
static constexpr bool kPs4RawComboProbeDebugLog = false;
static constexpr bool kPs4BridgeResetDebugLog = false;

static DWORD s_ps4BridgeResetDebugLastTickNoDs4 = 0;
static DWORD s_ps4BridgeResetDebugLastTickXinput = 0;

static void Win32_Ps4DebugLogBridgeResetIf(const wchar_t* reason)
{
    if (!kPs4BridgeResetDebugLog)
    {
        return;
    }
    if (!s_ps4LastReportB5to9Valid && !s_ps4BridgeDeltaHasPrev)
    {
        return;
    }
    const DWORD now = GetTickCount();
    const bool isNoDs4 = (wcscmp(reason, L"no_ds4_hid") == 0);
    DWORD& lastTick = isNoDs4 ? s_ps4BridgeResetDebugLastTickNoDs4 : s_ps4BridgeResetDebugLastTickXinput;
    // XInput 優先と DS4 未接続が短周期で交互に起きても、調査ログは理由別に 2s に 1 行まで
    if (lastTick != 0 && (now - lastTick) < 2000u)
    {
        return;
    }
    lastTick = now;
    wchar_t line[128] = {};
    swprintf_s(line, _countof(line), L"[PS4BridgeReset] %s\r\n", reason);
    OutputDebugStringW(line);
}

// T13: サンプル画面用（prevMove は毎フレーム変わり得るため Invalidate 判定から除外）
enum class MenuSampleUiLastEventKind : UINT8
{
    None = 0,
    Toggle,
    Move,
    Activate,
    Cancel,
};
static MenuSampleUiLastEventKind s_menuSampleUiLastEvent = MenuSampleUiLastEventKind::None;
static bool s_menuSamplePaintHasPrev = false;
static bool s_menuSamplePaintPrevOpen = false;
static INT8 s_menuSamplePaintPrevSelX = 0;
static INT8 s_menuSamplePaintPrevSelY = 0;
static MenuSampleUiLastEventKind s_menuSamplePaintPrevEvent = MenuSampleUiLastEventKind::None;

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
    out.psHome = (wb & XINPUT_GAMEPAD_GUIDE) != 0;
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

// DS4 USB (VID 054C / PID 05C4) 実機確認済みマップ（VirtualInput 橋渡し）
// T18: hat>=8 では dpad ビットは立てない（hat<8 のみ）。DPad+L1 同時の WM_INPUT で hatLo=8・L1=1 が来ることがあり（[PS4RawCombo]）、dpad=0000 でも生と整合。
// byte5: hat0-3, Share 0x10, Circle 0x20, Options 0x40, R3=b5&0x80（例: rawB5=0x88=hat8+R3）
// byte6: L1=b6&0x01（例: rawB6=0x01 + slot99 L1R1=10）, R1 0x02, L2 0x04, R2 0x08, Square 0x10, Cross 0x20, L3 0x40, Triangle 0x80
// byte7: PS 0x01
// byte8/9: L2/R2 アナログ
// VirtualInputPolicy: South=confirm, East=cancel, Start=Options(0x40), Select=Share(0x10), psHome=PS
static bool Win32_FillVirtualInputFromDs4StyleHidReport(const BYTE* buf, UINT len, VirtualInputSnapshot& out)
{
    if (buf == nullptr || len < 10)
    {
        return false;
    }

    out = {};
    out.connected = true;
    out.family = GameControllerKind::PlayStation4;

    auto axisU8 = [](BYTE v) -> SHORT
    {
        const int c = static_cast<int>(v) - 128;
        const int scaled = (std::max)(-32767, (std::min)(32767, c * 257));
        return static_cast<SHORT>(scaled);
    };

    out.leftStickX = axisU8(buf[1]);
    out.leftStickY = static_cast<SHORT>(-axisU8(buf[2]));
    out.rightStickX = axisU8(buf[3]);
    out.rightStickY = static_cast<SHORT>(-axisU8(buf[4]));

    const unsigned hat = buf[5] & 0x0FU;
    if (hat < 8)
    {
        out.dpadUp = (hat == 0 || hat == 1 || hat == 7);
        out.dpadRight = (hat == 1 || hat == 2 || hat == 3);
        out.dpadDown = (hat == 3 || hat == 4 || hat == 5);
        out.dpadLeft = (hat == 5 || hat == 6 || hat == 7);
    }

    out.select = (buf[5] & 0x10) != 0;
    out.east = (buf[5] & 0x20) != 0;
    out.start = (buf[5] & 0x40) != 0;
    out.r3 = (buf[5] & 0x80) != 0; // R3: b5&0x80（実測 rawB5=0x88, R3Only, slot99 L3R3=01）

    out.west = (buf[6] & 0x10) != 0;
    out.south = (buf[6] & 0x20) != 0;
    out.north = (buf[6] & 0x80) != 0;

    out.l1 = (buf[6] & 0x01) != 0; // L1: b6&0x01（実測 rawB6=0x01, L1Only, slot99 L1R1=10）
    out.r1 = (buf[6] & 0x02) != 0;
    out.l3 = (buf[6] & 0x40) != 0;

    out.leftTriggerRaw = buf[8];
    out.rightTriggerRaw = buf[9];
    out.l2Pressed = ((buf[6] & 0x04) != 0) || (buf[8] >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    out.r2Pressed = ((buf[6] & 0x08) != 0) || (buf[9] >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);

    out.psHome = (buf[7] & 0x01) != 0;

    out.leftInDeadzone = Win32_LeftStickInDeadzone(out.leftStickX, out.leftStickY);
    out.leftDir = Win32_ClassifyLeftStickDir(out.leftStickX, out.leftStickY, out.leftInDeadzone);
    out.rightInDeadzone = Win32_RightStickInDeadzone(out.rightStickX, out.rightStickY);
    out.rightDir = Win32_ClassifyLeftStickDir(out.rightStickX, out.rightStickY, out.rightInDeadzone);
    return true;
}

// === T25 [7] Win32: VirtualInput 系デバッグログ（snapshot / helper / policy / consumer / menu） ===
static void Win32_LogVirtualInputSnapshotSummary(const VirtualInputSnapshot& s, DWORD slot)
{
    wchar_t line[768] = {};
    swprintf_s(line, _countof(line),
        L"VirtualInput slot=%u fam=%s conn=%d "
        L"faceABXY=%d%d%d%d L1R1=%d%d L2R2=%d/%d raw=%u/%u L3R3=%d%d StSel=%d%d PS=%d "
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
        s.psHome ? 1 : 0,
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

static bool Win32_Ps4VirtualShoulderGroupChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    return (prev.l1 != curr.l1) || (prev.r1 != curr.r1)
        || (prev.l2Pressed != curr.l2Pressed) || (prev.r2Pressed != curr.r2Pressed)
        || (prev.leftTriggerRaw != curr.leftTriggerRaw)
        || (prev.rightTriggerRaw != curr.rightTriggerRaw)
        || (prev.l3 != curr.l3) || (prev.r3 != curr.r3);
}

static bool Win32_Ps4VirtualSlot99LogWorthy(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    if (Win32_Ps4VirtualShoulderGroupChanged(prev, curr))
    {
        return true;
    }
    if (prev.start != curr.start || prev.select != curr.select)
    {
        return true;
    }
    if (prev.psHome != curr.psHome)
    {
        return true;
    }
    return false;
}

// L1/R3/L3/Tri の「肩・顔・StSel・PS 以外の単独」候補。DPad は見ない（hat は Win32_LogVirtualInputPs4Slot99IsolateEdges で分岐）。
static bool Win32_Ps4VirtualIsolate_L1Candidate(const VirtualInputSnapshot& s)
{
    return s.l1 && !s.r1 && !s.l2Pressed && !s.r2Pressed && !s.l3 && !s.r3
        && !s.south && !s.east && !s.west && !s.north && !s.start && !s.select && !s.psHome;
}

static bool Win32_Ps4VirtualIsolate_R3Candidate(const VirtualInputSnapshot& s)
{
    return !s.l1 && !s.r1 && !s.l2Pressed && !s.r2Pressed && !s.l3 && s.r3
        && !s.south && !s.east && !s.west && !s.north && !s.start && !s.select && !s.psHome;
}

static bool Win32_Ps4VirtualIsolate_L3Candidate(const VirtualInputSnapshot& s)
{
    return !s.l1 && !s.r1 && !s.l2Pressed && !s.r2Pressed && s.l3 && !s.r3
        && !s.south && !s.east && !s.west && !s.north && !s.start && !s.select && !s.psHome;
}

static bool Win32_Ps4VirtualIsolate_TriCandidate(const VirtualInputSnapshot& s)
{
    return !s.l1 && !s.r1 && !s.l2Pressed && !s.r2Pressed && !s.l3 && !s.r3
        && !s.south && !s.east && !s.west && s.north && !s.start && !s.select && !s.psHome;
}

static bool Win32_Ps4VirtualIsolate_PSOnly(const VirtualInputSnapshot& s)
{
    return s.psHome && !s.l1 && !s.r1 && !s.l2Pressed && !s.r2Pressed && !s.l3 && !s.r3
        && !s.south && !s.east && !s.west && !s.north && !s.start && !s.select
        && !s.dpadUp && !s.dpadDown && !s.dpadLeft && !s.dpadRight;
}

static void Win32_LogVirtualInputPs4Slot99ShoulderGroupIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    DWORD slot)
{
    if (!Win32_Ps4VirtualSlot99LogWorthy(prev, curr))
    {
        return;
    }
    wchar_t line[768] = {};
    swprintf_s(line, _countof(line),
        L"[PS4VIchg] VirtualInput slot=%u fam=%s conn=%d "
        L"faceABXY=%d%d%d%d L1R1=%d%d L2R2=%d/%d raw=%u/%u L3R3=%d%d StSel=%d%d PS=%d "
        L"Dpad=%d%d%d%d "
        L"L(%d,%d)z=%d Ldir=%s R(%d,%d)z=%d Rdir=%s\r\n",
        static_cast<unsigned int>(slot),
        Win32_GameControllerKindLabel(curr.family),
        curr.connected ? 1 : 0,
        curr.south ? 1 : 0,
        curr.east ? 1 : 0,
        curr.west ? 1 : 0,
        curr.north ? 1 : 0,
        curr.l1 ? 1 : 0,
        curr.r1 ? 1 : 0,
        curr.l2Pressed ? 1 : 0,
        curr.r2Pressed ? 1 : 0,
        static_cast<unsigned int>(curr.leftTriggerRaw),
        static_cast<unsigned int>(curr.rightTriggerRaw),
        curr.l3 ? 1 : 0,
        curr.r3 ? 1 : 0,
        curr.start ? 1 : 0,
        curr.select ? 1 : 0,
        curr.psHome ? 1 : 0,
        curr.dpadUp ? 1 : 0,
        curr.dpadDown ? 1 : 0,
        curr.dpadLeft ? 1 : 0,
        curr.dpadRight ? 1 : 0,
        static_cast<int>(curr.leftStickX),
        static_cast<int>(curr.leftStickY),
        curr.leftInDeadzone ? 1 : 0,
        Win32_LeftStickDirLabel(curr.leftDir),
        static_cast<int>(curr.rightStickX),
        static_cast<int>(curr.rightStickY),
        curr.rightInDeadzone ? 1 : 0,
        Win32_LeftStickDirLabel(curr.rightDir));
    OutputDebugStringW(line);
}

static void Win32_LogVirtualInputPs4Slot99IsolateEdges(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    struct Iso
    {
        const wchar_t* tag;
        bool (*edgeCandidate)(const VirtualInputSnapshot&);
        bool splitHatForDs4Iso;
    };
    static const Iso kIsos[] = {
        { L"L1Only", Win32_Ps4VirtualIsolate_L1Candidate, true },
        { L"R3Only", Win32_Ps4VirtualIsolate_R3Candidate, true },
        { L"L3Only", Win32_Ps4VirtualIsolate_L3Candidate, true },
        { L"TriOnly", Win32_Ps4VirtualIsolate_TriCandidate, true },
        { L"PSOnly", Win32_Ps4VirtualIsolate_PSOnly, false },
    };
    for (const Iso& iso : kIsos)
    {
        if (iso.edgeCandidate(curr) && !iso.edgeCandidate(prev))
        {
            if (iso.splitHatForDs4Iso)
            {
                if (kPs4IsoProbeDebugLog)
                {
                    const unsigned int hatLo = s_ps4LastReportB5to9Valid
                        ? static_cast<unsigned int>(s_ps4LastReportB5 & 0x0FU)
                        : 0u;
                    wchar_t pr[256] = {};
                    swprintf_s(
                        pr,
                        _countof(pr),
                        L"[PS4ISOprobe] tag=%s cand=1 hatLo=%u dpad=%d%d%d%d valid=%d\r\n",
                        iso.tag,
                        hatLo,
                        curr.dpadUp ? 1 : 0,
                        curr.dpadDown ? 1 : 0,
                        curr.dpadLeft ? 1 : 0,
                        curr.dpadRight ? 1 : 0,
                        s_ps4LastReportB5to9Valid ? 1 : 0);
                    OutputDebugStringW(pr);
                }
                // hatLo==8: [PS4DS4ISO] / hatLo!=8: [PS4ISOskip]（コンボで hat=8 のフレームでは skip は出ない＝生レポートが中立のため。README T18）
                if (!s_ps4LastReportB5to9Valid || ((s_ps4LastReportB5 & 0x0FU) != 8u))
                {
                    if (kPs4IsoSkipDebugLog)
                    {
                        wchar_t sk[192] = {};
                        swprintf_s(
                            sk,
                            _countof(sk),
                            L"[PS4ISOskip] tag=%s hatLo=%u\r\n",
                            iso.tag,
                            s_ps4LastReportB5to9Valid
                                ? static_cast<unsigned int>(s_ps4LastReportB5 & 0x0FU)
                                : 0u);
                        OutputDebugStringW(sk);
                    }
                    continue;
                }
            }
            wchar_t line[384] = {};
            swprintf_s(line, _countof(line),
                L"[PS4DS4ISO] %s L1R1=%d%d L2R2=%d/%d raw=%u/%u L3R3=%d%d "
                L"face=%d%d%d%d StSel=%d%d PS=%d Dpad=%d%d%d%d hatLo=%u\r\n",
                iso.tag,
                curr.l1 ? 1 : 0,
                curr.r1 ? 1 : 0,
                curr.l2Pressed ? 1 : 0,
                curr.r2Pressed ? 1 : 0,
                static_cast<unsigned int>(curr.leftTriggerRaw),
                static_cast<unsigned int>(curr.rightTriggerRaw),
                curr.l3 ? 1 : 0,
                curr.r3 ? 1 : 0,
                curr.south ? 1 : 0,
                curr.east ? 1 : 0,
                curr.west ? 1 : 0,
                curr.north ? 1 : 0,
                curr.start ? 1 : 0,
                curr.select ? 1 : 0,
                curr.psHome ? 1 : 0,
                curr.dpadUp ? 1 : 0,
                curr.dpadDown ? 1 : 0,
                curr.dpadLeft ? 1 : 0,
                curr.dpadRight ? 1 : 0,
                static_cast<unsigned int>(s_ps4LastReportB5to9Valid ? (s_ps4LastReportB5 & 0x0FU) : 0u));
            OutputDebugStringW(line);
        }
    }
}

// slot=99: L1R1/L2R2/L3R3/PS または raw b5/b6/b8/b9 の変化時のみ要約（[PS4VIchg] とは別行）
static void Win32_LogPs4Slot99BridgeDeltaIfChanged(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    DWORD slot)
{
    if (!s_ps4HidVirtualFromLastReportValid || !s_ps4LastReportB5to9Valid)
    {
        return;
    }

    const BYTE b5 = s_ps4LastReportB5;
    const BYTE b6 = s_ps4LastReportB6;
    const BYTE b8 = s_ps4LastReportB8;
    const BYTE b9 = s_ps4LastReportB9;

    const bool viDelta = (prev.l1 != curr.l1) || (prev.r1 != curr.r1) || (prev.l2Pressed != curr.l2Pressed)
        || (prev.r2Pressed != curr.r2Pressed) || (prev.l3 != curr.l3) || (prev.r3 != curr.r3)
        || (prev.psHome != curr.psHome);
    const bool rawDelta = !s_ps4BridgeDeltaHasPrev || (b5 != s_ps4BridgeDeltaPrevB5)
        || (b6 != s_ps4BridgeDeltaPrevB6) || (b8 != s_ps4BridgeDeltaPrevB8) || (b9 != s_ps4BridgeDeltaPrevB9);

    if (!viDelta && !rawDelta)
    {
        return;
    }

    wchar_t line[320] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[PS4Bridge] slot=%u L1R1=%d%d L2R2=%d/%d L3R3=%d%d PS=%d rawB5=%02X rawB6=%02X rawB8=%02X rawB9=%02X\r\n",
        static_cast<unsigned int>(slot),
        curr.l1 ? 1 : 0,
        curr.r1 ? 1 : 0,
        curr.l2Pressed ? 1 : 0,
        curr.r2Pressed ? 1 : 0,
        curr.l3 ? 1 : 0,
        curr.r3 ? 1 : 0,
        curr.psHome ? 1 : 0,
        static_cast<unsigned int>(b5),
        static_cast<unsigned int>(b6),
        static_cast<unsigned int>(b8),
        static_cast<unsigned int>(b9));
    OutputDebugStringW(line);

    s_ps4BridgeDeltaPrevB5 = b5;
    s_ps4BridgeDeltaPrevB6 = b6;
    s_ps4BridgeDeltaPrevB8 = b8;
    s_ps4BridgeDeltaPrevB9 = b9;
    s_ps4BridgeDeltaHasPrev = true;
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

static const wchar_t* Win32_MenuSampleUiLastEventLabel(MenuSampleUiLastEventKind k)
{
    switch (k)
    {
    case MenuSampleUiLastEventKind::None: return L"none";
    case MenuSampleUiLastEventKind::Toggle: return L"toggle";
    case MenuSampleUiLastEventKind::Move: return L"move";
    case MenuSampleUiLastEventKind::Activate: return L"activate";
    case MenuSampleUiLastEventKind::Cancel: return L"cancel";
    default: return L"none";
    }
}

static void Win32_MenuSample_UpdateUiLastEventFromEvents(const VirtualInputMenuSampleEvents& ev)
{
    if (ev.menuToggled)
    {
        s_menuSampleUiLastEvent = MenuSampleUiLastEventKind::Toggle;
    }
    else if (ev.selectionChanged)
    {
        s_menuSampleUiLastEvent = MenuSampleUiLastEventKind::Move;
    }
    else if (ev.activated)
    {
        s_menuSampleUiLastEvent = MenuSampleUiLastEventKind::Activate;
    }
    else if (ev.cancelled)
    {
        s_menuSampleUiLastEvent = MenuSampleUiLastEventKind::Cancel;
    }
}

static void Win32_MenuSample_RequestInvalidateIfNeeded(HWND hwnd)
{
    if (!hwnd)
    {
        return;
    }
    const VirtualInputMenuSampleState& s = s_virtualInputMenuSampleState;
    if (!s_menuSamplePaintHasPrev)
    {
        s_menuSamplePaintHasPrev = true;
        s_menuSamplePaintPrevOpen = s.menuOpen;
        s_menuSamplePaintPrevSelX = s.selectionX;
        s_menuSamplePaintPrevSelY = s.selectionY;
        s_menuSamplePaintPrevEvent = s_menuSampleUiLastEvent;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    const bool stChanged =
        (s.menuOpen != s_menuSamplePaintPrevOpen) ||
        (s.selectionX != s_menuSamplePaintPrevSelX) ||
        (s.selectionY != s_menuSamplePaintPrevSelY);
    const bool evChanged = (s_menuSampleUiLastEvent != s_menuSamplePaintPrevEvent);
    if (stChanged || evChanged)
    {
        s_menuSamplePaintPrevOpen = s.menuOpen;
        s_menuSamplePaintPrevSelX = s.selectionX;
        s_menuSamplePaintPrevSelY = s.selectionY;
        s_menuSamplePaintPrevEvent = s_menuSampleUiLastEvent;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static void Win32_MenuSample_ResetPaintTracking(HWND hwnd)
{
    s_menuSampleUiLastEvent = MenuSampleUiLastEventKind::None;
    s_menuSamplePaintHasPrev = false;
    if (hwnd)
    {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// 左列: メニュー試作の 2x2 と menuOpen 表示。
static void Win32_FillMenuSamplePaintBuffers_MenuColumn(
    wchar_t* menuBuf,
    size_t menuBufCount,
    bool compactT37)
{
    const VirtualInputMenuSampleState& s = s_virtualInputMenuSampleState;
    const wchar_t* evLabel = Win32_MenuSampleUiLastEventLabel(s_menuSampleUiLastEvent);

    wchar_t c00[4] = L" ";
    wchar_t c10[4] = L" ";
    wchar_t c01[4] = L" ";
    wchar_t c11[4] = L" ";
    if (s.selectionX == 0 && s.selectionY == 0)
    {
        wcscpy_s(c00, _countof(c00), L"@");
    }
    else if (s.selectionX == 1 && s.selectionY == 0)
    {
        wcscpy_s(c10, _countof(c10), L"@");
    }
    else if (s.selectionX == 0 && s.selectionY == 1)
    {
        wcscpy_s(c01, _countof(c01), L"@");
    }
    else if (s.selectionX == 1 && s.selectionY == 1)
    {
        wcscpy_s(c11, _countof(c11), L"@");
    }

    if (compactT37)
    {
        swprintf_s(
            menuBuf,
            menuBufCount,
            L"[T37] Sample  mo=%d  (%d,%d)  %s\r\n"
            L"2x2 [%s][%s] / [%s][%s]\r\n",
            s.menuOpen ? 1 : 0,
            static_cast<int>(s.selectionX),
            static_cast<int>(s.selectionY),
            evLabel,
            c00,
            c10,
            c01,
            c11);
        return;
    }

    swprintf_s(menuBuf, menuBufCount,
        L"InputPlatformLab Sample\r\n\r\n"
        L"menuOpen: %d\r\n"
        L"selection: (%d,%d)\r\n"
        L"last event: %s\r\n\r\n"
        L"2x2 selection:\r\n"
        L"  [ %s ] [ %s ]\r\n"
        L"  [ %s ] [ %s ]\r\n",
        s.menuOpen ? 1 : 0,
        static_cast<int>(s.selectionX),
        static_cast<int>(s.selectionY),
        evLabel,
        c00, c10, c01, c11);
}

// T14 可視行 + T15 最近傍ブロック（モニタ無し時はプレースホルダ）。
static void Win32_FillMenuSamplePaintBuffers_T14T15Column(wchar_t* t14Buf, size_t t14BufCount)
{
    if (!s_displayMonitorsCache.empty() && kT14SelectedMonitorIndex < s_displayMonitorsCache.size())
    {
        const DisplayMonitorInfo& mon = s_displayMonitorsCache[kT14SelectedMonitorIndex];
        swprintf_s(t14Buf, t14BufCount,
            L"--- T14 Displays ---\r\n"
            L"monitor count: %zu\r\n"
            L"selected monitor index: %zu (fixed)\r\n"
            L"modes (deduped W*H): %zu\r\n"
            L"visibleModeCount: %zu\r\n"
            L"selectedModeIndex: %zu\r\n"
            L"firstVisibleModeIndex: %zu\r\n"
            L"(Up/Down: scroll when menu closed)\r\n"
            L"(Left/Right: T15 desired preset when menu closed)\r\n"
            L"visible modes:\r\n",
            s_displayMonitorsCache.size(),
            kT14SelectedMonitorIndex,
            mon.modes.size(),
            kT14VisibleModeCount,
            s_t14SelectedModeIndex,
            s_t14FirstVisibleModeIndex);

        for (size_t row = 0; row < kT14VisibleModeCount; ++row)
        {
            const size_t mi = s_t14FirstVisibleModeIndex + row;
            if (mi >= mon.modes.size())
            {
                break;
            }
            const DisplayModeInfo& mode = mon.modes[mi];
            const wchar_t* mark = (mi == s_t14SelectedModeIndex) ? L">" : L" ";
            const bool nearestStar =
                (s_t15MatchResult.nearestModeIndex != static_cast<size_t>(-1)) &&
                (mi == s_t15MatchResult.nearestModeIndex);
            wchar_t line[192] = {};
            swprintf_s(line, _countof(line),
                L"  %s%s [%zu] %dx%d bpp=%d hz=%d\r\n",
                mark,
                nearestStar ? L"*" : L" ",
                mi,
                mode.width,
                mode.height,
                mode.bits_per_pixel,
                mode.refresh_hz);
            wcscat_s(t14Buf, t14BufCount, line);
        }

        {
            wchar_t t15Block[768] = {};
            if (s_t15MatchResult.nearestModeIndex != static_cast<size_t>(-1) &&
                s_t15MatchResult.nearestModeIndex < mon.modes.size())
            {
                const DisplayModeInfo& nm = mon.modes[s_t15MatchResult.nearestModeIndex];
                swprintf_s(
                    t15Block,
                    _countof(t15Block),
                    L"\r\n--- T15 nearest resolution ---\r\n"
                    L"desired: %dx%d  preset[%zu/%zu]\r\n"
                    L"nearest: [%zu] %dx%d bpp=%d hz=%d\r\n"
                    L"delta: %d / %d\r\n"
                    L"exact match: %d\r\n",
                    s_t15DesiredWidth,
                    s_t15DesiredHeight,
                    s_t15DesiredPresetIndex,
                    kT15DesiredPresetCount,
                    s_t15MatchResult.nearestModeIndex,
                    nm.width,
                    nm.height,
                    nm.bits_per_pixel,
                    nm.refresh_hz,
                    s_t15MatchResult.deltaW,
                    s_t15MatchResult.deltaH,
                    s_t15MatchResult.exactMatch ? 1 : 0);
            }
            else
            {
                swprintf_s(
                    t15Block,
                    _countof(t15Block),
                    L"\r\n--- T15 nearest resolution ---\r\n"
                    L"desired: %dx%d  preset[%zu/%zu]\r\n"
                    L"nearest: (none)\r\n"
                    L"delta: - / -\r\n"
                    L"exact match: 0\r\n",
                    s_t15DesiredWidth,
                    s_t15DesiredHeight,
                    s_t15DesiredPresetIndex,
                    kT15DesiredPresetCount);
            }
            wcscat_s(t14Buf, t14BufCount, t15Block);
        }
    }
    else
    {
        swprintf_s(t14Buf, t14BufCount, L"--- T14 Displays ---\r\n(no monitors)\r\n");
    }
}

// T16 ウィンドウ情報 → T18 識別 → T17 表示モードの順で t14 バッファに連結。
static void Win32_FillMenuSamplePaintBuffers_AppendT16T18T17(
    HWND hwnd,
    const RECT& rcClient,
    wchar_t* t14Buf,
    size_t t14BufCount)
{
    Win32_T16_AppendPaintSection(t14Buf, t14BufCount, hwnd, rcClient);
    Win32_T18_AppendPaintSection(t14Buf, t14BufCount);
    Win32_T17_AppendPaintSection(t14Buf, t14BufCount);
}

// Win32DebugOverlay が WM_PAINT で読むバッファを組み立てる。T14〜T17 本文と T18 のスナップショットを含む。
void Win32_FillMenuSamplePaintBuffers(
    HWND hwnd,
    const RECT& rcClient,
    wchar_t* menuBuf,
    size_t menuBufCount,
    wchar_t* t14Buf,
    size_t t14BufCount,
    bool compactMenuForT37Layout)
{
    Win32_T18_RefreshControllerIdentifySnapshot();

    Win32_FillMenuSamplePaintBuffers_MenuColumn(menuBuf, menuBufCount, compactMenuForT37Layout);
    Win32_FillMenuSamplePaintBuffers_T14T15Column(t14Buf, t14BufCount);
    Win32_FillMenuSamplePaintBuffers_AppendT16T18T17(hwnd, rcClient, t14Buf, t14BufCount);
}

bool Win32_IsT37VirtualBodyOverlayActiveForLayout(void)
{
    return s_windowsRendererState.t37VirtualBodyOverlayRequested;
}

void Win32_FillMenuSamplePaintBuffers_MenuColumnOnly(wchar_t* menuBuf, size_t menuBufCount)
{
    Win32_FillMenuSamplePaintBuffers_MenuColumn(
        menuBuf,
        menuBufCount,
        Win32_IsT37VirtualBodyOverlayActiveForLayout());
}

// menuOpen 中は T14 行スクロールを動かさない前提。キャッシュされたレイアウトで選択行が見えるよう scroll を補正。
static void Win32_T14_TryAutoScrollSelectionIntoView(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        return;
    }
    if (s_virtualInputMenuSampleState.menuOpen)
    {
        return;
    }
    if (s_displayMonitorsCache.empty() ||
        kT14SelectedMonitorIndex >= s_displayMonitorsCache.size())
    {
        return;
    }

    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int clientW = static_cast<int>(rcClient.right - rcClient.left);
    const int clientH = static_cast<int>(rcClient.bottom - rcClient.top);

    if (!s_paintDbgT14LayoutValid || clientW != s_paintDbgClientW || clientH != s_paintDbgClientH)
    {
        OutputDebugStringW(
            L"[T14VIEW] source=paint-cache (skip; not ready or client size mismatch)\r\n");
        return;
    }

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hwnd, SB_VERT, &si))
    {
        return;
    }
    const int maxScroll = (std::max)(0, static_cast<int>(si.nMax) - static_cast<int>(si.nPage) + 1);
    const int scrollY = static_cast<int>(si.nPos);

    const ptrdiff_t rowSpan =
        static_cast<ptrdiff_t>(s_t14SelectedModeIndex) - static_cast<ptrdiff_t>(s_t14FirstVisibleModeIndex);
    if (rowSpan < 0 || rowSpan >= static_cast<ptrdiff_t>(kT14VisibleModeCount))
    {
        return;
    }

    const int lineHeight = s_paintDbgLineHeight;
    const int selectedRowTop =
        s_paintDbgT14VisibleModesDocStartY + static_cast<int>(rowSpan) * lineHeight;
    const int selectedRowBottom = selectedRowTop + lineHeight;

    static const int kT14ViewTopMargin = 12;
    static const int kT14ViewBottomMargin = 8;
    const int actualOverlayHeight = s_paintDbgActualOverlayHeight;
    const int anchorClientY =
        kT14ViewTopMargin + WIN32_T14_AUTOFOLLOW_ANCHOR_ROWS * lineHeight;

    wchar_t logLine[384] = {};
    OutputDebugStringW(L"[T14VIEW] source=paint-cache\r\n");
    OutputDebugStringW(L"[T14VIEW] mode=anchor-upper\r\n");
    swprintf_s(
        logLine,
        _countof(logLine),
        L"[T14VIEW] anchorRows=%d anchorClientY=%d\r\n",
        WIN32_T14_AUTOFOLLOW_ANCHOR_ROWS,
        anchorClientY);
    OutputDebugStringW(logLine);
    swprintf_s(
        logLine,
        _countof(logLine),
        L"[T14VIEW] visibleModesDocStartY=%d\r\n",
        s_paintDbgT14VisibleModesDocStartY);
    OutputDebugStringW(logLine);
    swprintf_s(logLine, _countof(logLine), L"[T14VIEW] selectedRowTop=%d\r\n", selectedRowTop);
    OutputDebugStringW(logLine);
    swprintf_s(logLine, _countof(logLine), L"[T14VIEW] selectedRowBottom=%d\r\n", selectedRowBottom);
    OutputDebugStringW(logLine);
    swprintf_s(logLine, _countof(logLine), L"[T14VIEW] actualOverlayHeight=%d\r\n", actualOverlayHeight);
    OutputDebugStringW(logLine);

    const int contentSafeHeight =
        clientH - actualOverlayHeight - kT14ViewBottomMargin - kT14ViewTopMargin;
    if (contentSafeHeight < lineHeight)
    {
        swprintf_s(
            logLine,
            _countof(logLine),
            L"[T14VIEW] autoFollow scrollY: before=%d after=%d (skip; viewport too small)\r\n",
            scrollY,
            scrollY);
        OutputDebugStringW(logLine);
        return;
    }

    int candidate = selectedRowTop - anchorClientY;
    candidate = (std::clamp)(candidate, 0, maxScroll);

    const int safeBottomAfterAnchor =
        candidate + clientH - actualOverlayHeight - kT14ViewBottomMargin;
    if (selectedRowBottom > safeBottomAfterAnchor)
    {
        candidate += (selectedRowBottom - safeBottomAfterAnchor);
    }

    const int clamped = (std::clamp)(candidate, 0, maxScroll);

    const int safeTopFinal = clamped + kT14ViewTopMargin;
    const int safeBottomFinal = clamped + clientH - actualOverlayHeight - kT14ViewBottomMargin;
    swprintf_s(
        logLine,
        _countof(logLine),
        L"[T14VIEW] safeTop=%d safeBottom=%d\r\n",
        safeTopFinal,
        safeBottomFinal);
    OutputDebugStringW(logLine);
    swprintf_s(
        logLine,
        _countof(logLine),
        L"[T14VIEW] autoFollow scrollY: before=%d after=%d\r\n",
        scrollY,
        clamped);
    OutputDebugStringW(logLine);

    if (clamped != scrollY)
    {
        Win32DebugOverlay_MainView_SetScrollPos(hwnd, clamped, nullptr);
    }
}

static void Win32_LogVirtualInputMenuSampleIfChanged(
    const VirtualInputConsumerFrame& unifiedFrame,
    HWND hwndForPaint)
{
    const VirtualInputMenuSampleEvents ev =
        VirtualInputMenuSample_Apply(s_virtualInputMenuSampleState, unifiedFrame);
    Win32_MenuSample_UpdateUiLastEventFromEvents(ev);
    Win32_MenuSample_RequestInvalidateIfNeeded(hwndForPaint);
    Win32_LogVirtualInputMenuSample_Events(ev, s_virtualInputMenuSampleState);
    Win32_LogVirtualInputMenuSample_StateDumpIfChanged(s_virtualInputMenuSampleState);
}

static void Win32_UnifiedInputMenuTick_ClearPendingT17IfMenuOpen(void)
{
    if (s_virtualInputMenuSampleState.menuOpen)
    {
        s_t17PendingApplyRequest = false;
    }
}

// menuOpen が閉じているときのみ: T14/T15/T17 のキー、スクロール、WM_INPUT でラッチした Enter による T17 apply 消費。
static void Win32_UnifiedInputMenuTick_WhenMenuClosed(HWND hwndForPaint)
{
    if (!hwndForPaint || s_virtualInputMenuSampleState.menuOpen)
    {
        return;
    }

    bool t17F6Edge = false;
    bool t17F5Edge = false;
    const bool upEdge = !s_keyboardActionStateAtLastTimer.up && s_keyboardActionState.up;
    const bool downEdge = !s_keyboardActionStateAtLastTimer.down && s_keyboardActionState.down;
    const bool leftEdge = !s_keyboardActionStateAtLastTimer.left && s_keyboardActionState.left;
    const bool rightEdge = !s_keyboardActionStateAtLastTimer.right && s_keyboardActionState.right;
    t17F6Edge = !s_keyboardActionStateAtLastTimer.f6 && s_keyboardActionState.f6;
    t17F5Edge = !s_keyboardActionStateAtLastTimer.f5 && s_keyboardActionState.f5;
    if (t17F5Edge)
    {
        wcscpy_s(s_t17LastKeyAffectingT17, _countof(s_t17LastKeyAffectingT17), L"F5 (no T17 action)");
        wcscpy_s(
            s_t17F5UnrelatedHint,
            _countof(s_t17F5UnrelatedHint),
            L"Reminder: F5 does not cycle or apply T17 — use F6 (cycle) and Enter (apply).");
        OutputDebugStringW(L"[T17] F5 edge: no T17 action (F6=cycle, Enter=apply)\r\n");
        InvalidateRect(hwndForPaint, nullptr, FALSE);
    }
    if (upEdge || downEdge)
    {
        Win32_T14_TryScrollFromKeyboardEdges(upEdge, downEdge, hwndForPaint);
    }
    if (leftEdge || rightEdge)
    {
        Win32_T15_TryChangePresetFromKeyboardEdges(leftEdge, rightEdge, hwndForPaint);
    }
    if (t17F6Edge)
    {
        ++s_t17CycleSeq;
        wcscpy_s(s_t17LastKeyAffectingT17, _countof(s_t17LastKeyAffectingT17), L"F6 (mode cycle)");
        s_t17F5UnrelatedHint[0] = L'\0';
        Win32_T17_CyclePresentationMode(hwndForPaint);
    }
    {
        const bool pageUpEdge =
            !s_keyboardActionStateAtLastTimer.pageUp && s_keyboardActionState.pageUp;
        const bool pageDownEdge =
            !s_keyboardActionStateAtLastTimer.pageDown && s_keyboardActionState.pageDown;
        const bool homeEdge = !s_keyboardActionStateAtLastTimer.home && s_keyboardActionState.home;
        const bool endEdge = !s_keyboardActionStateAtLastTimer.end && s_keyboardActionState.end;
        const bool f7Edge = !s_keyboardActionStateAtLastTimer.f7 && s_keyboardActionState.f7;
        const bool f8Edge = !s_keyboardActionStateAtLastTimer.f8 && s_keyboardActionState.f8;
        if (pageUpEdge)
        {
            SendMessageW(hwndForPaint, WM_VSCROLL, SB_PAGEUP, 0);
        }
        if (pageDownEdge)
        {
            SendMessageW(hwndForPaint, WM_VSCROLL, SB_PAGEDOWN, 0);
        }
        if (homeEdge)
        {
            SendMessageW(hwndForPaint, WM_VSCROLL, SB_TOP, 0);
        }
        if (endEdge)
        {
            SendMessageW(hwndForPaint, WM_VSCROLL, SB_BOTTOM, 0);
        }
        if (f7Edge)
        {
            Win32DebugOverlay_MainView_SetScrollPos(
                hwndForPaint,
                Win32DebugOverlay_ScrollTargetT17WithTopMargin(),
                L"F7 jump to T17 (top margin)");
        }
        if (f8Edge)
        {
            Win32DebugOverlay_MainView_SetScrollPos(
                hwndForPaint,
                Win32DebugOverlay_ScrollTargetT17Centered(hwndForPaint),
                L"F8 center T17 in view");
        }
    }
    if (s_t17PendingApplyRequest)
    {
        s_t17PendingApplyRequest = false;
        OutputDebugStringW(L"[T17] APPLY CONSUME pending request\r\n");
        wcscpy_s(s_t17LastKeyAffectingT17, _countof(s_t17LastKeyAffectingT17), L"Enter (apply)");
        s_t17F5UnrelatedHint[0] = L'\0';
        Win32_T17_ApplyCurrentPresentationMode(hwndForPaint);
    }
}

// キーとパッドの ConsumerFrame をマージしメニュー試作へ。末尾でタイマー境界のキー状態を進める。
static void Win32_UnifiedInputMenuTick_MergeAndApply(HWND hwndForPaint)
{
    const VirtualInputConsumerFrame kbFrame =
        VirtualInputConsumer_BuildFrameFromKeyboardState(
            s_keyboardActionStateAtLastTimer,
            s_keyboardActionState);
    const VirtualInputConsumerFrame ctrlFrame =
        VirtualInputConsumer_BuildFrame(s_virtualInputPrev, s_virtualInputCurr);
    const VirtualInputConsumerFrame unified =
        VirtualInputConsumer_MergeKeyboardController(kbFrame, ctrlFrame);
    Win32_LogVirtualInputMenuSampleIfChanged(unified, hwndForPaint);
    s_keyboardActionStateAtLastTimer = s_keyboardActionState;
}

// menuOpen が true の間は T14/T15/T17 のキーボード操作を抑止（矢印は 2x2 メニュー用）。閉じているときだけ表示系デバッグ操作を処理。
static void Win32_UnifiedInputConsumerMenuTick(HWND hwndForPaint)
{
    Win32_UnifiedInputMenuTick_ClearPendingT17IfMenuOpen();
    Win32_UnifiedInputMenuTick_WhenMenuClosed(hwndForPaint);
    Win32_UnifiedInputMenuTick_MergeAndApply(hwndForPaint);
}

// ---------------------------------------------------------------------------
// タイマー: XInput ポーリング（先頭スロット）と、キー／パッドを統合したメニュー試作の 1 フレーム
// ---------------------------------------------------------------------------

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

static bool s_ps4DpadProbeHasPrev = false;
static BYTE s_ps4DpadProbePrevB5Lo = 0;
static UINT8 s_ps4DpadProbePrevDpadMask = 0;

static bool s_ps4RawComboProbeHasPrev = false;
static BYTE s_ps4RawComboProbePrevB5Lo = 0;
static bool s_ps4RawComboProbePrevL1 = false;
static UINT32 s_ps4RawComboProbeSeq = 0;

static void Win32_Ps4DpadProbeResetState()
{
    s_ps4DpadProbeHasPrev = false;
    s_ps4RawComboProbeHasPrev = false;
}

// slot=99 タイマー経路: rawB5 下位ニブルと curr.dpad* の変化のみ観測（DPad 単独実測用）
static void Win32_LogPs4DpadProbeIfChanged(const VirtualInputSnapshot& curr)
{
    if (!kPs4DPadProbeDebugLog || !s_ps4LastReportB5to9Valid)
    {
        return;
    }
    const BYTE b5Lo = static_cast<BYTE>(s_ps4LastReportB5 & 0x0FU);
    const UINT8 dpadMask = static_cast<UINT8>(
        (curr.dpadUp ? 1u : 0u) << 3u | (curr.dpadDown ? 1u : 0u) << 2u | (curr.dpadLeft ? 1u : 0u) << 1u
        | (curr.dpadRight ? 1u : 0u));
    if (!s_ps4DpadProbeHasPrev)
    {
        s_ps4DpadProbeHasPrev = true;
        s_ps4DpadProbePrevB5Lo = b5Lo;
        s_ps4DpadProbePrevDpadMask = dpadMask;
        return;
    }
    const bool b5LoChanged = (b5Lo != s_ps4DpadProbePrevB5Lo);
    const bool dpadChanged = (dpadMask != s_ps4DpadProbePrevDpadMask);
    if (!b5LoChanged && !dpadChanged)
    {
        return;
    }
    const wchar_t* reason =
        (b5LoChanged && dpadChanged) ? L"b5_lo+dpad" : (b5LoChanged ? L"b5_lo" : L"dpad");
    wchar_t line[256] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[PS4DPadProbe] rawB5=%02X hatLo=%u dpad=%d%d%d%d reason=%s\r\n",
        static_cast<unsigned int>(s_ps4LastReportB5),
        static_cast<unsigned int>(b5Lo),
        curr.dpadUp ? 1 : 0,
        curr.dpadDown ? 1 : 0,
        curr.dpadLeft ? 1 : 0,
        curr.dpadRight ? 1 : 0,
        reason);
    OutputDebugStringW(line);
    s_ps4DpadProbePrevB5Lo = b5Lo;
    s_ps4DpadProbePrevDpadMask = dpadMask;
}

// 先頭スロットの XInput を VirtualInput に反映。未接続時は DS4 Raw レポートがあればそちらを優先。
static void Win32_XInputPollDigitalEdgesOnTimer(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);

    constexpr GameControllerKind kFamily = GameControllerKind::Xbox;

    const DWORD slot = Win32_GetFirstConnectedXInputSlotOrMax();
    if (slot >= XUSER_MAX_COUNT)
    {
        // レイヤー 2: known DS4 HID が WM_INPUT で届いていれば VirtualInput を更新（DS4 verified マップ）
        if (s_ps4HidVirtualFromLastReportValid)
        {
            constexpr DWORD kPs4PseudoSlot = 99;
            s_virtualInputPrev = s_virtualInputCurr;
            s_virtualInputCurr = s_ps4HidVirtualFromLastReport;
            Win32_LogPs4DpadProbeIfChanged(s_virtualInputCurr);
            Win32_LogVirtualInputPs4Slot99ShoulderGroupIfChanged(
                s_virtualInputPrev, s_virtualInputCurr, kPs4PseudoSlot);
            Win32_LogPs4Slot99BridgeDeltaIfChanged(
                s_virtualInputPrev, s_virtualInputCurr, kPs4PseudoSlot);
            Win32_LogVirtualInputPs4Slot99IsolateEdges(s_virtualInputPrev, s_virtualInputCurr);
            Win32_LogVirtualInputPolicyIfChanged(s_virtualInputPrev, s_virtualInputCurr);
            Win32_LogVirtualInputConsumerIfChanged(s_virtualInputPrev, s_virtualInputCurr);
            return;
        }

        Win32_Ps4DebugLogBridgeResetIf(L"no_ds4_hid");
        Win32_Ps4DpadProbeResetState();
        s_ps4LastReportB5to9Valid = false;
        s_ps4BridgeDeltaHasPrev = false;
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
        // keyboard 由来の VirtualInputMenuSample（Tab の menuOpen 等）は XInput 不在のたびに VirtualInputMenuSample_Reset しない
        // keyboard edge 履歴は Win32_UnifiedInputConsumerMenuTick 末尾でのみ更新する（ここで同期すると矢印エッジが消える）
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
        return;
    }

    s_ps4HidVirtualFromLastReportValid = false;
    Win32_Ps4DebugLogBridgeResetIf(L"xinput_active");
    Win32_Ps4DpadProbeResetState();
    s_ps4LastReportB5to9Valid = false;
    s_ps4BridgeDeltaHasPrev = false;

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
        Win32_MenuSample_ResetPaintTracking(s_mainWindowHwnd);
        return;
    }

    Win32_LogVirtualInputPolicyIfChanged(s_virtualInputPrev, s_virtualInputCurr);
    Win32_LogVirtualInputConsumerIfChanged(s_virtualInputPrev, s_virtualInputCurr);

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
        traits.vendor_id = static_cast<UINT16>(info.hid.dwVendorId);
        traits.product_id = static_cast<UINT16>(info.hid.dwProductId);
        traits.usage_page = info.hid.usUsagePage;
        traits.usage = info.hid.usUsage;

        if (!Win32_HidTraitsLookLikeGamepad(traits))
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
        ControllerParserKind pk{};
        ControllerSupportLevel sl{};
        Win32_ResolveHidProductTable(traits.vendor_id, traits.product_id, pk, sl);

        wchar_t line[1024] = {};
        swprintf_s(line, _countof(line),
            L"Gamepad: kind=%s vid=0x%04X pid=0x%04X usage=0x%04X/0x%04X xinput_any=%d name=\"%s\" path=\"%s\" "
            L"parser=%s support=%s\r\n",
            Win32_GameControllerKindLabel(kind),
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

// === T18: XInput 先頭スロット + Raw Input 先頭 HID ゲームパッド（1 台・WM_PAINT で更新） ===
static void Win32_T18_LogIfChanged()
{
    const bool same =
        s_t18HasLogPrev &&
        (s_t18.hid_found == s_t18LogPrev.hid_found) &&
        (s_t18.xinput_slot == s_t18LogPrev.xinput_slot) &&
        (s_t18.hid.vendor_id == s_t18LogPrev.hid.vendor_id) &&
        (s_t18.hid.product_id == s_t18LogPrev.hid.product_id) &&
        (s_t18.inferred_kind == s_t18LogPrev.inferred_kind) &&
        (s_t18.parser_kind == s_t18LogPrev.parser_kind) &&
        (s_t18.support_level == s_t18LogPrev.support_level) &&
        (wcscmp(s_t18.product_name, s_t18LogPrev.product_name) == 0) &&
        (wcscmp(s_t18.device_path, s_t18LogPrev.device_path) == 0);
    if (same)
    {
        return;
    }
    s_t18LogPrev = s_t18;
    s_t18HasLogPrev = true;

    const unsigned vid = s_t18.hid_found ? static_cast<unsigned>(s_t18.hid.vendor_id) : 0u;
    const unsigned pid = s_t18.hid_found ? static_cast<unsigned>(s_t18.hid.product_id) : 0u;
    wchar_t line[2048] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T18] hid_found=%d slot=%d vid=0x%04X pid=0x%04X family=%s parser=%s support=%s product=\"%s\" path=\"%s\"\r\n",
        s_t18.hid_found ? 1 : 0,
        s_t18.xinput_slot,
        vid,
        pid,
        Win32_GameControllerKindFamilyLabel(s_t18.inferred_kind),
        Win32_ControllerParserKindLabel(s_t18.parser_kind),
        Win32_ControllerSupportLevelLabel(s_t18.support_level),
        (s_t18.product_name[0] != L'\0') ? s_t18.product_name : L"",
        (s_t18.device_path[0] != L'\0') ? s_t18.device_path : L"");
    OutputDebugStringW(line);
}

static void Win32_T18_RefreshControllerIdentifySnapshot()
{
    T18ControllerIdentifySnapshot snap{};
    snap.xinput_slot = -1;
    snap.hid_found = false;
    snap.hid.device_info_valid = false;
    snap.inferred_kind = GameControllerKind::Unknown;
    snap.parser_kind = ControllerParserKind::None;
    snap.support_level = ControllerSupportLevel::Tentative;

    const DWORD slotDw = Win32_GetFirstConnectedXInputSlotOrMax();
    if (slotDw < XUSER_MAX_COUNT)
    {
        snap.xinput_slot = static_cast<int>(slotDw);
    }

    const bool anyXInput = Win32_QueryAnyXInputConnected();

    UINT numDevices = 0;
    if (GetRawInputDeviceList(nullptr, &numDevices, sizeof(RAWINPUTDEVICELIST)) != static_cast<UINT>(-1) &&
        numDevices > 0)
    {
        std::vector<RAWINPUTDEVICELIST> devices(numDevices);
        UINT copyCount = numDevices;
        if (GetRawInputDeviceList(devices.data(), &copyCount, sizeof(RAWINPUTDEVICELIST)) !=
            static_cast<UINT>(-1))
        {
            for (UINT i = 0; i < copyCount && !snap.hid_found; ++i)
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
                traits.vendor_id = static_cast<UINT16>(info.hid.dwVendorId);
                traits.product_id = static_cast<UINT16>(info.hid.dwProductId);
                traits.usage_page = info.hid.usUsagePage;
                traits.usage = info.hid.usUsage;

                if (!Win32_HidTraitsLookLikeGamepad(traits))
                {
                    continue;
                }

                wchar_t pathBuf[512] = {};
                wchar_t productBuf[256] = {};
                Win32_TryGetRawInputDeviceString(hDevice, RIDI_DEVICENAME, pathBuf, _countof(pathBuf));
                Win32_TryGetRawInputDeviceString(hDevice, RIDI_PRODUCTNAME, productBuf, _countof(productBuf));

                const bool productUsable =
                    (productBuf[0] != L'\0') && !Win32_T18_RawInputProductLooksLikeDevicePath(productBuf);
                const wchar_t* productPtr = productUsable ? productBuf : nullptr;
                const wchar_t* pathPtr = (pathBuf[0] != L'\0') ? pathBuf : nullptr;

                snap.hid = traits;
                snap.hid_found = true;
                wcscpy_s(snap.device_path, pathBuf);
                if (productUsable)
                {
                    wcscpy_s(snap.product_name, productBuf);
                }

                snap.inferred_kind =
                    Win32_ClassifyGameControllerKind(traits, productPtr, pathPtr, anyXInput);
                Win32_ResolveHidProductTable(
                    traits.vendor_id,
                    traits.product_id,
                    snap.parser_kind,
                    snap.support_level);
                break;
            }
        }
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

    s_t18 = snap;
    Win32_T18_LogIfChanged();
}

// Raw Input キーから KeyboardActionState を更新。タイマー境界で prev→curr エッジを取る材料。
static void Win32_UpdateKeyboardActionStateFromPhysicalKey(const PhysicalKeyEvent& ev)
{
    const UINT vk = static_cast<UINT>(ev.native_key_code);
    bool* target = nullptr;
    switch (vk)
    {
    case VK_UP: target = &s_keyboardActionState.up; break;
    case VK_DOWN: target = &s_keyboardActionState.down; break;
    case VK_LEFT: target = &s_keyboardActionState.left; break;
    case VK_RIGHT: target = &s_keyboardActionState.right; break;
    case VK_RETURN: target = &s_keyboardActionState.enter; break;
    case VK_BACK: target = &s_keyboardActionState.backspace; break;
    case VK_TAB: target = &s_keyboardActionState.tab; break;
    case VK_F6: target = &s_keyboardActionState.f6; break;
    case VK_F5: target = &s_keyboardActionState.f5; break;
    case VK_PRIOR: target = &s_keyboardActionState.pageUp; break;
    case VK_NEXT: target = &s_keyboardActionState.pageDown; break;
    case VK_HOME: target = &s_keyboardActionState.home; break;
    case VK_END: target = &s_keyboardActionState.end; break;
    case VK_F7: target = &s_keyboardActionState.f7; break;
    case VK_F8: target = &s_keyboardActionState.f8; break;
    default: return;
    }
    *target = !ev.is_key_up;
}

// === T25 [1] 続き: PhysicalKeyEvent の Raw Input 取得・表示ラベル・デバッグ行 ===
static void Win32_FillPhysicalKeyFromRawKeyboard(const RAWKEYBOARD& kb, PhysicalKeyEvent& out)
{
    out.native_key_code = static_cast<UINT16>(kb.VKey);
    out.scan_code = kb.MakeCode;
    out.is_extended_0 = (kb.Flags & RI_KEY_E0) != 0;
    out.is_extended_1 = (kb.Flags & RI_KEY_E1) != 0;
    out.is_key_up = (kb.Flags & RI_KEY_BREAK) != 0;
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

    Win32_FillPhysicalKeyFromRawKeyboard(raw.data.keyboard, out);
    return true;
}

// Sony DS4 (USB 0x05C4 / BT 0x09CC 等): WM_INPUT の RIM_TYPEHID を調査ログし VirtualInput に反映
static void Win32_TryLogRawInputHidPs4AndBridge(const RAWINPUT* raw)
{
    if (raw == nullptr || raw->header.dwType != RIM_TYPEHID)
    {
        return;
    }

    const HANDLE hDev = raw->header.hDevice;
    RID_DEVICE_INFO info = {};
    info.cbSize = sizeof(info);
    UINT cb = sizeof(info);
    if (GetRawInputDeviceInfo(hDev, RIDI_DEVICEINFO, &info, &cb) == static_cast<UINT>(-1))
    {
        return;
    }
    if (info.dwType != RIM_TYPEHID)
    {
        return;
    }

    const UINT vid = info.hid.dwVendorId;
    const UINT pid = info.hid.dwProductId;
    const USHORT up = info.hid.usUsagePage;
    const USHORT u = info.hid.usUsage;

    const RAWHID& hid = raw->data.hid;
    const UINT nBytes = hid.dwSizeHid * hid.dwCount;
    if (nBytes == 0)
    {
        return;
    }
    const BYTE* p = hid.bRawData;

    const bool isSonyDs4Like = (vid == 0x054C) && (pid == 0x05C4 || pid == 0x09CC);
    if (!isSonyDs4Like)
    {
        return;
    }

    VirtualInputSnapshot snap{};
    const bool parseOk = Win32_FillVirtualInputFromDs4StyleHidReport(p, nBytes, snap);
    if (parseOk)
    {
        s_ps4HidVirtualFromLastReport = snap;
        s_ps4HidVirtualFromLastReportValid = true;
    }
    if (parseOk && nBytes >= 10)
    {
        s_ps4LastReportB5 = p[5];
        s_ps4LastReportB6 = p[6];
        s_ps4LastReportB8 = p[8];
        s_ps4LastReportB9 = p[9];
        s_ps4LastReportB5to9Valid = true;

        // s_ps4HidVirtualFromLastReport は上の parseOk ブロックで既に snap と同期済み。hatLo/L1 変化時のみ WM_INPUT 生遷移を 1 行。
        if (kPs4RawComboProbeDebugLog)
        {
            const BYTE b5Lo = static_cast<BYTE>(p[5] & 0x0FU);
            const bool l1Now = (p[6] & 0x01) != 0;
            if (!s_ps4RawComboProbeHasPrev)
            {
                s_ps4RawComboProbeHasPrev = true;
                s_ps4RawComboProbePrevB5Lo = b5Lo;
                s_ps4RawComboProbePrevL1 = l1Now;
            }
            else
            {
                const bool b5LoChanged = (b5Lo != s_ps4RawComboProbePrevB5Lo);
                const bool l1Changed = (l1Now != s_ps4RawComboProbePrevL1);
                if (b5LoChanged || l1Changed)
                {
                    ++s_ps4RawComboProbeSeq;
                    wchar_t rc[256] = {};
                    swprintf_s(
                        rc,
                        _countof(rc),
                        L"[PS4RawCombo] b5=%02X hatLo=%u b6=%02X l1=%d seq=%u dpad=%d%d%d%d\r\n",
                        static_cast<unsigned int>(p[5]),
                        static_cast<unsigned int>(b5Lo),
                        static_cast<unsigned int>(p[6]),
                        l1Now ? 1 : 0,
                        static_cast<unsigned int>(s_ps4RawComboProbeSeq),
                        snap.dpadUp ? 1 : 0,
                        snap.dpadDown ? 1 : 0,
                        snap.dpadLeft ? 1 : 0,
                        snap.dpadRight ? 1 : 0);
                    OutputDebugStringW(rc);
                    s_ps4RawComboProbePrevB5Lo = b5Lo;
                    s_ps4RawComboProbePrevL1 = l1Now;
                }
            }
        }
    }

    if (kPs4HidVerboseRawLog)
    {
        bool btnBytesChanged = false;
        if (nBytes >= 8)
        {
            btnBytesChanged =
                !s_ps4HasPrevBtn567 || (memcmp(s_ps4PrevBtn567, p + 5, 3) != 0);
        }
    
        wchar_t xorHex[32] = L"-";
        if (nBytes >= 8 && s_ps4HasPrevBytes0to7)
        {
            xorHex[0] = L'\0';
            for (int i = 0; i < 8; ++i)
            {
                swprintf_s(
                    xorHex + i * 3,
                    _countof(xorHex) - static_cast<size_t>(i * 3),
                    L"%02X ",
                    static_cast<unsigned int>(static_cast<BYTE>(p[i] ^ s_ps4PrevBytes0to7[i])));
            }
        }
    
        const unsigned hat = (nBytes > 5) ? (p[5] & 0x0FU) : 0u;
        const unsigned b5HighNibble = (nBytes > 5) ? ((p[5] >> 4) & 0x0FU) : 0u;
        const unsigned faceBits = (nBytes > 6) ? p[6] : 0u;
    
        wchar_t ps4btn[1024] = {};
        swprintf_s(
            ps4btn,
            _countof(ps4btn),
            L"[PS4BTN] policy=South(b6&0x20) East(b5&0x20) Tri(b6&0x80) menu(b5&0x40) R3(b5&0x80) "
            L"L1(b6&0x01) R1(b6&0x02) L2d(b6&0x04) R2d(b6&0x08) L3(b6&0x40) "
            L"b0=%02X b1=%02X b2=%02X b3=%02X b4=%02X b5=%02X b6=%02X b7=%02X b8=%02X b9=%02X "
            L"buttonsNibble=%u b5_hiNibble=%u faceBits=0x%02X dpad=%u "
            L"parsedSouth=%d parsedEast=%d parsedMenu=%d parsedSel=%d "
            L"btnBytesChanged=%d xor0_7=[%s]\r\n",
            (nBytes > 0) ? p[0] : 0u,
            (nBytes > 1) ? p[1] : 0u,
            (nBytes > 2) ? p[2] : 0u,
            (nBytes > 3) ? p[3] : 0u,
            (nBytes > 4) ? p[4] : 0u,
            (nBytes > 5) ? p[5] : 0u,
            (nBytes > 6) ? p[6] : 0u,
            (nBytes > 7) ? p[7] : 0u,
            (nBytes > 8) ? p[8] : 0u,
            (nBytes > 9) ? p[9] : 0u,
            hat,
            b5HighNibble,
            faceBits,
            hat,
            parseOk && snap.south ? 1 : 0,
            parseOk && snap.east ? 1 : 0,
            parseOk && snap.start ? 1 : 0,
            parseOk && snap.select ? 1 : 0,
            btnBytesChanged ? 1 : 0,
            xorHex);
    
        OutputDebugStringW(ps4btn);
    
        if (btnBytesChanged && nBytes >= 8)
        {
            const unsigned c6_40 = (p[6] >> 6) & 1u;
            const unsigned c6_80 = (p[6] >> 7) & 1u;
            wchar_t ps4map[512] = {};
            swprintf_s(
                ps4map,
                _countof(ps4map),
                L"[PS4MAP] raw(b5,b6,b7)=%02X,%02X,%02X parsedSouth=%d parsedEast=%d parsedMenu=%d parsedSel=%d "
                L"candL3_b6_40=%u candTri_b6_80=%u candMenu_b5_40=%u candR3_b5_80=%u\r\n",
                p[5],
                p[6],
                p[7],
                parseOk && snap.south ? 1 : 0,
                parseOk && snap.east ? 1 : 0,
                parseOk && snap.start ? 1 : 0,
                parseOk && snap.select ? 1 : 0,
                c6_40,
                c6_80,
                (p[5] & 0x40) != 0 ? 1u : 0u,
                (p[5] & 0x80) != 0 ? 1u : 0u);
            OutputDebugStringW(ps4map);
        }
    
        // Circle(○) 切り分け: ○ 単押し想定。byte7 のみの差分（周期ノイズ）では発火しない。b2/b5/b6/b7 の候補のみ。
        wchar_t ps4CircleDiffIdx[48] = L"-";
        if (s_ps4HasPrevB5to8 && nBytes >= 9)
        {
            ps4CircleDiffIdx[0] = L'\0';
            int pos = 0;
            for (int bi = 5; bi <= 8; ++bi)
            {
                if (p[bi] != s_ps4PrevB5to8[bi - 5])
                {
                    if (pos > 0)
                    {
                        ps4CircleDiffIdx[pos++] = L',';
                    }
                    pos += swprintf_s(
                        ps4CircleDiffIdx + pos,
                        _countof(ps4CircleDiffIdx) - static_cast<size_t>(pos),
                        L"%d",
                        bi);
                }
            }
            if (pos == 0)
            {
                wcscpy_s(ps4CircleDiffIdx, L"none");
            }
        }
    
        const bool ps4CircleSkipB7OnlyNoise =
            s_ps4HasPrevB5to8 && (wcscmp(ps4CircleDiffIdx, L"7") == 0);
    
        // diffIdx が "7" のみ = byte7 だけが前回から変化（周期アナログ等）→ PS4CIRCLE は出さない
        if (btnBytesChanged && nBytes >= 9 && parseOk && s_ps4HasPrevB5to8 && !ps4CircleSkipB7OnlyNoise)
        {
            const bool circleProbe =
                !snap.south && !snap.start && !snap.select && !snap.psHome &&
                !snap.dpadUp && !snap.dpadDown && !snap.dpadLeft && !snap.dpadRight &&
                !snap.l1 && !snap.r1 && !snap.l3 && !snap.r3;
    
            if (circleProbe)
            {
                const BYTE prev2 = (s_ps4HasPrevBytes0to7 && nBytes > 2) ? s_ps4PrevBytes0to7[2] : 0;
                const BYTE cur2 = (nBytes > 2) ? p[2] : 0;
                const BYTE xor2 = (nBytes > 2 && s_ps4HasPrevBytes0to7) ? static_cast<BYTE>(p[2] ^ prev2) : 0;
    
                const BYTE prev5 = s_ps4PrevB5to8[0];
                const BYTE prev6 = s_ps4PrevB5to8[1];
                const BYTE prev7 = s_ps4PrevB5to8[2];
                const BYTE xor5 = static_cast<BYTE>(p[5] ^ prev5);
                const BYTE xor6 = static_cast<BYTE>(p[6] ^ prev6);
                const BYTE xor7 = static_cast<BYTE>(p[7] ^ prev7);
    
                const unsigned c_b2_01 = (cur2 & 0x01) != 0 ? 1u : 0u;
                const unsigned c_b2_02 = (cur2 & 0x02) != 0 ? 1u : 0u;
                const unsigned c_b2_04 = (cur2 & 0x04) != 0 ? 1u : 0u;
                const unsigned c_b2_08 = (cur2 & 0x08) != 0 ? 1u : 0u;
                const unsigned c_b5_40 = (p[5] & 0x40) != 0 ? 1u : 0u;
                const unsigned c_b5_80 = (p[5] & 0x80) != 0 ? 1u : 0u;
                const unsigned c_b6_10 = (p[6] & 0x10) != 0 ? 1u : 0u;
                const unsigned c_b6_20 = (p[6] & 0x20) != 0 ? 1u : 0u;
                const unsigned c_b6_40 = (p[6] & 0x40) != 0 ? 1u : 0u;
                const unsigned c_b6_80 = (p[6] & 0x80) != 0 ? 1u : 0u;
                const unsigned c_b7_01 = (p[7] & 0x01) != 0 ? 1u : 0u;
                const unsigned c_b7_02 = (p[7] & 0x02) != 0 ? 1u : 0u;
                const unsigned c_b7_04 = (p[7] & 0x04) != 0 ? 1u : 0u;
                const unsigned c_b7_08 = (p[7] & 0x08) != 0 ? 1u : 0u;
                const unsigned c_b7_10 = (p[7] & 0x10) != 0 ? 1u : 0u;
                const unsigned c_b7_20 = (p[7] & 0x20) != 0 ? 1u : 0u;
                const unsigned c_b7_40 = (p[7] & 0x40) != 0 ? 1u : 0u;
                const unsigned c_b7_80 = (p[7] & 0x80) != 0 ? 1u : 0u;
    
                wchar_t ps4circ[1024] = {};
                swprintf_s(
                    ps4circ,
                    _countof(ps4circ),
                    L"[PS4CIRCLE] diffIdx=%s "
                    L"b2=%02X prev2=%02X xor2=%02X b5=%02X prev5=%02X xor5=%02X "
                    L"b6=%02X prev6=%02X xor6=%02X b7=%02X prev7=%02X xor7=%02X "
                    L"cand_b2(01,02,04,08)=%u%u%u%u cand_b5(40,80)=%u%u "
                    L"cand_b6(10,20,40,80)=%u%u%u%u cand_b7(01,02,04,08,10,20,40,80)=%u%u%u%u%u%u%u%u "
                    L"parsedEast=%d parsedNorth=%d\r\n",
                    ps4CircleDiffIdx,
                    static_cast<unsigned int>(cur2),
                    static_cast<unsigned int>(prev2),
                    static_cast<unsigned int>(xor2),
                    static_cast<unsigned int>(p[5]),
                    static_cast<unsigned int>(prev5),
                    static_cast<unsigned int>(xor5),
                    static_cast<unsigned int>(p[6]),
                    static_cast<unsigned int>(prev6),
                    static_cast<unsigned int>(xor6),
                    static_cast<unsigned int>(p[7]),
                    static_cast<unsigned int>(prev7),
                    static_cast<unsigned int>(xor7),
                    c_b2_01,
                    c_b2_02,
                    c_b2_04,
                    c_b2_08,
                    c_b5_40,
                    c_b5_80,
                    c_b6_10,
                    c_b6_20,
                    c_b6_40,
                    c_b6_80,
                    c_b7_01,
                    c_b7_02,
                    c_b7_04,
                    c_b7_08,
                    c_b7_10,
                    c_b7_20,
                    c_b7_40,
                    c_b7_80,
                    snap.east ? 1 : 0,
                    snap.north ? 1 : 0);
                OutputDebugStringW(ps4circ);
            }
        }
    
        if (parseOk)
        {
            const bool noDpad =
                !snap.dpadUp && !snap.dpadDown && !snap.dpadLeft && !snap.dpadRight;
            const bool noMisc =
                !snap.select && !snap.l3 && !snap.l1 && !snap.r1 && !snap.psHome;
            if (snap.south && !snap.east && !snap.west && !snap.north && !snap.start && noMisc && noDpad)
            {
                OutputDebugStringW(L"[PS4BTN] isolate singleFace=South(Cross)_only\r\n");
            }
            if (snap.east && !snap.south && !snap.west && !snap.north && !snap.start && noMisc && noDpad)
            {
                OutputDebugStringW(L"[PS4BTN] isolate singleFace=East(Circle)_only\r\n");
            }
            if (snap.start && !snap.south && !snap.east && !snap.west && !snap.north && !snap.select && !snap.psHome &&
                !snap.l3 && !snap.r3 && !snap.l1 && !snap.r1 && noDpad)
            {
                OutputDebugStringW(L"[PS4BTN] isolate singleMenu=Options_only\r\n");
            }
        }
    
        wchar_t hidline[512] = {};
        swprintf_s(
            hidline,
            _countof(hidline),
            L"[PS4HID] usagePage=0x%04X usage=0x%04X sizeHid=%u count=%u payload=%u btnBytesChanged=%d\r\n",
            static_cast<unsigned int>(up),
            static_cast<unsigned int>(u),
            static_cast<unsigned int>(hid.dwSizeHid),
            static_cast<unsigned int>(hid.dwCount),
            static_cast<unsigned int>(nBytes),
            btnBytesChanged ? 1 : 0);
    
        OutputDebugStringW(hidline);
    }

    if (nBytes >= 8)
    {
        memcpy(s_ps4PrevBtn567, p + 5, 3);
        s_ps4HasPrevBtn567 = true;
        memcpy(s_ps4PrevBytes0to7, p, 8);
        s_ps4HasPrevBytes0to7 = true;
    }
    if (nBytes >= 9)
    {
        memcpy(s_ps4PrevB5to8, p + 5, 4);
        s_ps4HasPrevB5to8 = true;
    }

    if (nBytes <= sizeof(s_ps4InvestPrevReport))
    {
        memcpy(s_ps4InvestPrevReport, p, nBytes);
        s_ps4InvestPrevReportLen = nBytes;
        s_ps4InvestHasPrev = true;
    }
}

// WM_INPUT: XInput はタイマー側。ここは Raw HID — 既知 DS4 橋渡し or 汎用 HID 要約。
// [HIDgen]: 同一 VID/PID は kHidGenericLogMinIntervalMs に 1 行まで（usage/payload はコレクションで変わりうるためスロットルキーに含めない）
static constexpr DWORD kHidGenericLogMinIntervalMs = 1000u;
static constexpr bool kHidGenThrottleDebugLog = false; // true で [HIDgenDbg]（調査時のみ）
static DWORD s_hidGenericLastLogTick = 0;
static UINT16 s_hidGenericLastVid = 0;
static UINT16 s_hidGenericLastPid = 0;

static bool Win32_FillGameControllerHidSummaryFromRawInput(const RAWINPUT* raw, GameControllerHidSummary& out)
{
    if (raw == nullptr || raw->header.dwType != RIM_TYPEHID)
    {
        return false;
    }
    const HANDLE hDev = raw->header.hDevice;
    RID_DEVICE_INFO info = {};
    info.cbSize = sizeof(info);
    UINT cb = sizeof(info);
    if (GetRawInputDeviceInfo(hDev, RIDI_DEVICEINFO, &info, &cb) == static_cast<UINT>(-1))
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

static void Win32_LogGenericHidGamepadFallback(const RAWINPUT* raw, const GameControllerHidSummary& t)
{
    const RAWHID& hid = raw->data.hid;
    const UINT nBytes = hid.dwSizeHid * hid.dwCount;
    ControllerParserKind pk{};
    ControllerSupportLevel sl{};
    Win32_ResolveHidProductTable(t.vendor_id, t.product_id, pk, sl);
    const DWORD now = GetTickCount();
    const bool sameVidPid =
        (t.vendor_id == s_hidGenericLastVid) && (t.product_id == s_hidGenericLastPid);
    const DWORD dtMs =
        (s_hidGenericLastLogTick == 0) ? 0xFFFFFFFFu : (now - s_hidGenericLastLogTick);
    if (kHidGenThrottleDebugLog)
    {
        wchar_t dbg[320] = {};
        swprintf_s(
            dbg,
            _countof(dbg),
            L"[HIDgenDbg] sameVP=%d dt=%u vid=0x%04X pid=0x%04X usage=0x%04X/0x%04X payload=%u\r\n",
            sameVidPid ? 1 : 0,
            static_cast<unsigned int>(dtMs),
            static_cast<unsigned int>(t.vendor_id),
            static_cast<unsigned int>(t.product_id),
            static_cast<unsigned int>(t.usage_page),
            static_cast<unsigned int>(t.usage),
            static_cast<unsigned int>(nBytes));
        OutputDebugStringW(dbg);
    }
    if (sameVidPid && dtMs < kHidGenericLogMinIntervalMs)
    {
        return;
    }
    s_hidGenericLastLogTick = now;
    s_hidGenericLastVid = t.vendor_id;
    s_hidGenericLastPid = t.product_id;
    wchar_t line[384] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[HIDgen] vid=0x%04X pid=0x%04X usage=0x%04X/0x%04X payload=%u parser=%s support=%s (tentative bucket; no verified map)\r\n",
        static_cast<unsigned int>(t.vendor_id),
        static_cast<unsigned int>(t.product_id),
        static_cast<unsigned int>(t.usage_page),
        static_cast<unsigned int>(t.usage),
        static_cast<unsigned int>(nBytes),
        Win32_ControllerParserKindLabel(pk),
        Win32_ControllerSupportLevelLabel(sl));
    OutputDebugStringW(line);
}

static void Win32_OnRawInputHidGamepadLayers(const RAWINPUT* raw)
{
    GameControllerHidSummary t{};
    if (!Win32_FillGameControllerHidSummaryFromRawInput(raw, t) || !Win32_HidTraitsLookLikeGamepad(t))
    {
        return;
    }
    ControllerParserKind pk{};
    ControllerSupportLevel sl{};
    Win32_ResolveHidProductTable(t.vendor_id, t.product_id, pk, sl);
    if (pk == ControllerParserKind::Ds4KnownHid && sl == ControllerSupportLevel::Verified)
    {
        Win32_TryLogRawInputHidPs4AndBridge(raw);
        return;
    }
    Win32_LogGenericHidGamepadFallback(raw, t);
}

// ToUnicodeEx 用 keyState: 修飾キー自身の make では従来どおり空配列（GetKeyNameTextW フォールバック）を優先する。
// 非修飾キーでは Shift のみ GetAsyncKeyState で反映（Ctrl/Alt/AltGr/CapsLock は T07 スコープ外）。
static bool Win32_IsModifierVirtualKeyForLabel(UINT vk)
{
    switch (vk)
    {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return true;
    default:
        return false;
    }
}

static void Win32_ApplyShiftOnlyToKeyStateForToUnicodeEx(BYTE keyState[256])
{
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
    {
        keyState[VK_SHIFT] |= 0x80;
    }
    if ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0)
    {
        keyState[VK_LSHIFT] |= 0x80;
    }
    if ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0)
    {
        keyState[VK_RSHIFT] |= 0x80;
    }
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

    if (!Win32_IsModifierVirtualKeyForLabel(vk))
    {
        Win32_ApplyShiftOnlyToKeyStateForToUnicodeEx(keyState);
    }

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

static void Win32_WndProc_OnRawInput(LPARAM lParam)
{
    UINT dwSize = 0;
    if (GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT,
            nullptr,
            &dwSize,
            sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
    {
        return;
    }
    if (dwSize == 0)
    {
        return;
    }
    std::vector<BYTE> inputBuf(dwSize);
    if (GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT,
            inputBuf.data(),
            &dwSize,
            sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
    {
        return;
    }
    const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(inputBuf.data());

    if (raw->header.dwType == RIM_TYPEHID)
    {
        Win32_OnRawInputHidGamepadLayers(raw);
    }
    else if (raw->header.dwType == RIM_TYPEKEYBOARD)
    {
        PhysicalKeyEvent ev{};
        Win32_FillPhysicalKeyFromRawKeyboard(raw->data.keyboard, ev);
        Win32_UpdateKeyboardActionStateFromPhysicalKey(ev);
        if (static_cast<UINT>(ev.native_key_code) == VK_RETURN && !ev.is_key_up &&
            !s_virtualInputMenuSampleState.menuOpen)
        {
            s_t17PendingApplyRequest = true;
            OutputDebugStringW(L"[T17] ENTER MAKE latched apply request\r\n");
        }
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

// WM_TIMER: 先に XInput（内部で DS4 Raw 優先など）、続けてキー+パッドの統合メニュー 1 フレーム。
static void Win32_WndProc_OnXInputPollTimer(HWND hWnd)
{
    Win32_XInputPollDigitalEdgesOnTimer(hWnd);
    Win32_UnifiedInputConsumerMenuTick(hWnd);
}

// T31: WM_SIZE → GetClientRect → WindowsRenderer_Resize（ResizeBuffers / RTV）
static void Win32_WndProc_OnClientSize(HWND hWnd)
{
    RECT cr{};
    GetClientRect(hWnd, &cr);
    const UINT32 cw = static_cast<UINT32>(
                (std::max)(0, static_cast<int>(cr.right - cr.left)));
    const UINT32 ch = static_cast<UINT32>(
                (std::max)(0, static_cast<int>(cr.bottom - cr.top)));
    WindowsRenderer_Resize(&s_windowsRendererState, cw, ch);
}

static bool Win32_WndProc_OnVScroll(
    HWND hWnd,
    WPARAM wParam,
    LPARAM lParam,
    UINT message,
    LRESULT* outDefResult)
{
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hWnd, SB_VERT, &si))
    {
        return false;
    }
    const int y = static_cast<int>(si.nPos);
    const int nMin = static_cast<int>(si.nMin);
    const int nMax = static_cast<int>(si.nMax);
    const UINT nPage = si.nPage;
    const int maxScroll = (std::max)(0, nMax - static_cast<int>(nPage) + 1);
    int newY = y;
    switch (LOWORD(wParam))
    {
    case SB_LINEUP:
        newY -= (s_paintScrollLinePx > 0) ? s_paintScrollLinePx : WIN32_MAIN_DEBUG_SCROLL_LINE;
        break;
    case SB_LINEDOWN:
        newY += (s_paintScrollLinePx > 0) ? s_paintScrollLinePx : WIN32_MAIN_DEBUG_SCROLL_LINE;
        break;
    case SB_PAGEUP:
        newY -= (static_cast<int>(nPage) * WIN32_MAIN_SCROLL_PAGEUP_NUM) / WIN32_MAIN_SCROLL_PAGEUP_DEN;
        break;
    case SB_PAGEDOWN:
        newY += (static_cast<int>(nPage) * WIN32_MAIN_SCROLL_PAGEUP_NUM) / WIN32_MAIN_SCROLL_PAGEUP_DEN;
        break;
    case SB_TOP:
        newY = nMin;
        break;
    case SB_BOTTOM:
        newY = maxScroll;
        break;
    case SB_THUMBTRACK:
        {
            SCROLLINFO st = {};
            st.cbSize = sizeof(st);
            st.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(hWnd, SB_VERT, &st))
            {
                newY = static_cast<int>(st.nTrackPos);
            }
        }
        break;
    case SB_THUMBPOSITION:
        newY = static_cast<int>(static_cast<short>(HIWORD(wParam)));
        break;
    default:
        *outDefResult = DefWindowProc(hWnd, message, wParam, lParam);
        return true;
    }
    newY = (std::clamp)(newY, 0, maxScroll);
    if (newY != y)
    {
        si.fMask = SIF_POS;
        si.nPos = newY;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        s_paintScrollY = newY;
        {
            wchar_t wv[96] = {};
            swprintf_s(
                wv,
                _countof(wv),
                L"WM_VSCROLL SB=%d",
                static_cast<int>(LOWORD(wParam)));
            Win32DebugOverlay_ScrollLog(wv, hWnd, y, newY, -1, -1, -1, -1);
        }
        InvalidateRect(hWnd, nullptr, FALSE);
    }
    return false;
}

static void Win32_WndProc_OnMouseWheel(HWND hWnd, WPARAM wParam)
{
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(hWnd, SB_VERT, &si))
    {
        return;
    }
    const int y = static_cast<int>(si.nPos);
    UINT wheelLines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);
    const int linePx = (s_paintScrollLinePx > 0) ? s_paintScrollLinePx : WIN32_MAIN_DEBUG_SCROLL_LINE;
    const int pagePx = static_cast<int>(si.nPage);
    const int quarterPage = (std::max)(pagePx / 4, linePx * 3);
    int dy = 0;
    if (wheelLines == WHEEL_PAGESCROLL)
    {
        dy = pagePx;
    }
    else
    {
        const int lineBased = static_cast<int>(wheelLines) * linePx;
        dy = (std::max)(lineBased, quarterPage);
    }
    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    int newY = y;
    if (delta > 0)
    {
        newY -= dy;
    }
    else if (delta < 0)
    {
        newY += dy;
    }
    const int maxScroll = (std::max)(0, static_cast<int>(si.nMax) - static_cast<int>(si.nPage) + 1);
    newY = (std::clamp)(newY, 0, maxScroll);
    if (newY != y)
    {
        si.fMask = SIF_POS;
        si.nPos = newY;
        SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
        s_paintScrollY = newY;
        Win32DebugOverlay_ScrollLog(L"WM_MOUSEWHEEL", hWnd, y, newY, -1, -1, -1, -1);
        InvalidateRect(hWnd, nullptr, FALSE);
    }
}

// T30/T33: 1 フレーム — D3D clear → D2D 1 行（renderer）→ Present →（オプション）GDI オーバーレイ
static void Win32_MainView_PaintFrame(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);
    // T26/T33: renderer（clear → D2D 1 行 → Present）→ 任意で GDI debug overlay
    Win32_RefreshRendererGridDebugParams(hWnd);
    Win32_T37_PrepareVirtualBodyOverlay(hWnd);
    swprintf_s(
        s_windowsRendererState.t17HudLine,
        _countof(s_windowsRendererState.t17HudLine),
        L"cand=%s act=%s",
        Win32_T17_ModeLabel(s_t17CurrentPresentationMode),
        Win32_T17_ModeLabel(s_t17LastAppliedPresentationMode));
#if !WIN32_MAIN_D3D_CLEAR_ONLY_PAINT && !WIN32_MAIN_T33_HIDE_GDI_OVERLAY
    Win32_DebugOverlay_PrefillHudLeftColumnForD2d(
        hWnd,
        hdc,
        &s_windowsRendererState,
        Win32_T17_ModeLabel(s_t17LastAppliedPresentationMode));
#endif
    WindowsRenderer_Frame(
        &s_windowsRendererState,
        hWnd,
        Win32_MapT17ToRendererPresentationMode(s_t17LastAppliedPresentationMode));
#if !WIN32_MAIN_D3D_CLEAR_ONLY_PAINT && !WIN32_MAIN_T33_HIDE_GDI_OVERLAY
    {
        const bool suppressT14BodyGdi =
            s_windowsRendererState.dbgHudLeftColumnSkipGdi ||
            (s_windowsRendererState.t37VirtualBodyOverlayRequested &&
             s_windowsRendererState.t37VirtualBodyOverlayRenderedOk);
        Win32DebugOverlay_Paint(
            hWnd,
            hdc,
            Win32_T17_ModeLabel(s_t17LastAppliedPresentationMode),
            suppressT14BodyGdi,
            s_windowsRendererState.dbgHudLeftColumnSkipGdi);
    }
#endif
    EndPaint(hWnd, &ps);
}

static void Win32_WndProc_OnPaint(HWND hWnd)
{
    Win32_MainView_PaintFrame(hWnd);
}

//
// WndProc: メインウィンドウ。入力は WM_INPUT（キー・HID）と WM_TIMER（XInput・統合メニュー）、
// 描画は WM_PAINT で D3D クリア後に GDI オーバーレイ。サイズ・スクロールは WM_SIZE / WM_VSCROLL / WM_MOUSEWHEEL。
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        // 既定の背景消去（白塗り）を抑止し、D3D の swap chain 表示を優先する
        return 1;
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
    // Raw Input: ゲームパッド HID は即時、キーは KeyboardActionState 更新。Enter make は menu 閉じ時のみ T17 適用をラッチ。
    case WM_INPUT:
        Win32_WndProc_OnRawInput(lParam);
        return 0;
    // 約 33ms: XInput 更新のあと、キーとパッドをマージして VirtualInputMenuSample などを進める。
    case WM_TIMER:
        if (wParam == TIMER_ID_XINPUT_POLL)
        {
            Win32_WndProc_OnXInputPollTimer(hWnd);
        }
        break;
    // クライアント変化に合わせてスワップチェーンをリサイズし、再描画を予約。
    case WM_SIZE:
        Win32_WndProc_OnClientSize(hWnd);
        Win32DebugOverlay_ScrollLog(L"WM_SIZE", hWnd, s_paintScrollY, s_paintScrollY, -1, -1, -1, -1);
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    // タイトルバー移動などのあと GDI オーバーレイだけ欠けることがあるため、再描画を強制（D3D 経路は WM_PAINT 内の従来処理）。
    case WM_EXITSIZEMOVE:
        RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        break;
    case WM_MOVE:
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    case WM_WINDOWPOSCHANGED:
        InvalidateRect(hWnd, nullptr, FALSE);
        break;
    // デバッグ本文の縦スクロール（行・ページ・ホーム／エンド・サムトラック）。
    case WM_VSCROLL:
        {
            LRESULT defRes = 0;
            if (Win32_WndProc_OnVScroll(hWnd, wParam, lParam, message, &defRes))
            {
                return defRes;
            }
        }
        break;
    // ホイールでも WM_VSCROLL と同様に s_paintScrollY を更新。
    case WM_MOUSEWHEEL:
        Win32_WndProc_OnMouseWheel(hWnd, wParam);
        break;
    // D3D で背景クリア・Present のあと、GDI でテキストオーバーレイ（スクロールクリップ・下段 [scroll] 表示）。
    case WM_PAINT:
        Win32_WndProc_OnPaint(hWnd);
        break;
    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID_XINPUT_POLL);
        if (s_t16DestroyIsRecreate)
        {
            s_t16DestroyIsRecreate = false;
        }
        else
        {
            if (s_t17FullscreenDisplayAppliedNow)
            {
                Win32_T17_ResetMonitor0DisplaySettings();
            }
            WindowsRenderer_Shutdown(&s_windowsRendererState);
            s_mainWindowHwnd = nullptr;
            PostQuitMessage(0);
        }
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
