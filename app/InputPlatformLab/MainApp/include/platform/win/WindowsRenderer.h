// T23/T24/T25: Windows 描画・DirectX 11 最小レンダラ
// T31: 公開面は Init / Resize / Frame の 3 役割（+ ライフサイクル Shutdown）。
// T33: Frame 内で clear → D2D/DWrite（1 行）→ Present。失敗時は clear→Present のみ。
// 確認用: WIN32_RENDERER_DEBUG_GRID_64PX で D2D ストロークの可変グリッド（分母は適用済みターゲット幅。D3D ライン API は未使用）。
//
// MainApp の GDI デバッグ描画は WM_PAINT 内で Frame の後に実行（上書き合成、WIN32_MAIN_T33_HIDE_GDI_OVERLAY で比較可）。
#pragma once

// MainApp と WindowsRenderer で同じ値に揃える（未設定時はデバッググリッド有効）。
#ifndef WIN32_RENDERER_DEBUG_GRID_64PX
#define WIN32_RENDERER_DEBUG_GRID_64PX 1
#endif

#include "CommonTypes.h"
#include "framework.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi.h>

#include <cstdint>

// デバッググリッドのセル幅の分母（1280@64 スケール）。CommittedSelected = Enter 確定時の T14 選択幅、Client = 未確定時フォールバック。
enum class WindowsRendererGridDebugBasis : std::uint8_t
{
    Client = 0,
    CommittedSelected = 1,
};

// T35: MainApp の T17 と同順序（Frame で T34 ガードに使用）
enum class WindowsRendererPresentationMode : std::uint8_t
{
    Windowed = 0,
    Borderless = 1,
    Fullscreen = 2,
};

// D3D 初期化時のクライアントサイズ（物理ピクセル想定）
struct WindowsRendererConfig
{
    UINT32 clientWidth = 0;
    UINT32 clientHeight = 0;
};

// スワップチェーンと RTV を保持。T33: D2D/DWrite は同一スワップチェーン表面へ 1 行描画。
struct WindowsRendererState
{
    bool initialized = false;
    HWND targetHwnd = nullptr;
    UINT32 clientWidth = 0;
    UINT32 clientHeight = 0;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    // T34（Borderless のみ）: committed 解像度のオフスクリーン RT → swapchain へ合成。T17 のウィンドウサイズとは別軸。Frame は presentationMode でガード。
    ID3D11Texture2D* borderlessOffscreenTexture = nullptr;
    ID3D11RenderTargetView* borderlessOffscreenRtv = nullptr;
    bool borderlessOffscreenComposite = false;
    std::uint32_t borderlessOffscreenPhysW = 0;
    std::uint32_t borderlessOffscreenPhysH = 0;
    // T36（Fullscreen 実験のみ）: committed 解像度のオフスクリーン RT → swapchain へ合成。T34 と別リソース。
    ID3D11Texture2D* fullscreenOffscreenTexture = nullptr;
    ID3D11RenderTargetView* fullscreenOffscreenRtv = nullptr;
    bool fullscreenOffscreenComposite = false;
    std::uint32_t fullscreenOffscreenPhysW = 0;
    std::uint32_t fullscreenOffscreenPhysH = 0;
    // T33: D3D11 と共有（IDXGIDevice）。Resize ではデバイス再作成せず、Frame で都度 DXGI 表面からビットマップ生成。
    ID2D1Factory1* d2dFactory = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;
    ID2D1Device* d2dDevice = nullptr;
    ID2D1DeviceContext* d2dContext = nullptr;
    IDWriteTextFormat* dwriteTextFormat = nullptr;
    ID2D1SolidColorBrush* d2dTextBrush = nullptr;
    // デバッググリッド（WIN32_RENDERER_DEBUG_GRID_64PX、オプション）。Frame 前に MainApp が target を書き込む。
    ID2D1SolidColorBrush* d2dGridBrush = nullptr;
    ID2D1SolidColorBrush* d2dGridBrushEm = nullptr;
    IDWriteTextFormat* dwriteGridLabelFormat = nullptr;
    WindowsRendererGridDebugBasis gridDebugBasis = WindowsRendererGridDebugBasis::Client;
    std::uint32_t gridDebugDenomPhysW = 0; // 0 のときは clientWidth を分母にフォールバック（Enter 確定の選択幅）
    std::uint32_t gridDebugCommittedPhysW = 0;
    std::uint32_t gridDebugCommittedPhysH = 0;
    std::uint32_t gridDebugClientPhysW = 0;
    std::uint32_t gridDebugClientPhysH = 0;
    // T37: Borderless(T34)/Fullscreen(T36) オフスクリーン有効時、本文を仮想 committed 解像度基準で DWrite 描画（GDI はフォールバック可）
    bool t37VirtualBodyOverlayRequested = false;
    bool t37VirtualBodyOverlayRenderedOk = false;
    int t37ScrollVirtualPx = 0;
    wchar_t t37BodyText[8192]{};
    // T17: F6 候補と Enter 適用を D2D 上段 1 行に表示（MainApp が毎フレーム更新）
    wchar_t t17HudLine[128]{};
};

// --- T31 公開面（3 入口 + Shutdown）---
// Init:   デバイス・スワップチェーン・初回 RTV。DLL はこの呼び出しでロードされる。
// Resize: WM_SIZE 相当のクライアント幅・高さ → ResizeBuffers + RTV 再作成。
// Frame:  対象 HWND が一致するとき、RTV へ bind・viewport・clear →（任意）デバッググリッド →（T33）D2D 1 行 → Present（GDI は呼ばない）。
bool WindowsRenderer_Init(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState);
void WindowsRenderer_Shutdown(WindowsRendererState* state);
void WindowsRenderer_Resize(WindowsRendererState* state, UINT32 clientW, UINT32 clientH);
void WindowsRenderer_Frame(
    WindowsRendererState* state,
    HWND hwnd,
    WindowsRendererPresentationMode presentationMode);
// T35: Borderless 以外へ遷移したときにオフスクリーン RT/フラグを確実に捨てる（MainApp の Refresh からも呼ぶ）
void WindowsRenderer_ClearBorderlessOffscreen(WindowsRendererState* state);
// T36: Fullscreen 以外へ遷移したときにオフスクリーン RT/フラグを捨てる（Refresh からも呼ぶ）
void WindowsRenderer_ClearFullscreenOffscreen(WindowsRendererState* state);
// デバッググリッド: [GRID] mode=... の 1 回ログをリセット（Enter 確定直後の再描画で再度出したいとき）
void WindowsRenderer_DebugGrid_ResetLogOnce(void);
