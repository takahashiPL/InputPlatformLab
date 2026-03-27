// MainApp.cpp : アプリケーションのエントリ ポイントを定義します。
//

#include "framework.h"
#include "MainApp.h"

#include <cstdint>
#include <stdio.h>

#define MAX_LOADSTRING 100

// プラットフォーム中立な物理キーイベント（将来 input/ 配下へ移設可能）
struct PhysicalKeyEvent
{
    std::uint16_t native_key_code; // Win32 では仮想キー（VK）に相当
    std::uint16_t scan_code;       // スキャンコード（拡張プレフィックスは is_extended_* と併用）
    bool is_extended_0;            // 拡張キー前置 E0 相当
    bool is_extended_1;            // 拡張キー前置 E1 相当
    bool is_key_up;                // 離上（break）なら true
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

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

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

    const int gkn = GetKeyNameTextW(lParam, buffer, static_cast<int>(bufferCount));
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
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: HDC を使用する描画コードをここに追加してください...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
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
