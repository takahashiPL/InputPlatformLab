// T23/T24/T25: Windows 描画・DirectX 11 最小レンダラ
// T31: 公開面は Init / Resize / Frame の 3 役割（+ ライフサイクル Shutdown）。
// T33: Frame 内で clear → D2D/DWrite（1 行）→ Present。失敗時は clear→Present のみ。
//
// MainApp の GDI デバッグ描画は WM_PAINT 内で Frame の後に実行（上書き合成、WIN32_MAIN_T33_HIDE_GDI_OVERLAY で比較可）。
#pragma once

#include "CommonTypes.h"
#include "framework.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi.h>

#include <cstdint>

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
    // T33: D3D11 と共有（IDXGIDevice）。Resize ではデバイス再作成せず、Frame で都度 DXGI 表面からビットマップ生成。
    ID2D1Factory1* d2dFactory = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;
    ID2D1Device* d2dDevice = nullptr;
    ID2D1DeviceContext* d2dContext = nullptr;
    IDWriteTextFormat* dwriteTextFormat = nullptr;
    ID2D1SolidColorBrush* d2dTextBrush = nullptr;
};

// --- T31 公開面（3 入口 + Shutdown）---
// Init:   デバイス・スワップチェーン・初回 RTV。DLL はこの呼び出しでロードされる。
// Resize: WM_SIZE 相当のクライアント幅・高さ → ResizeBuffers + RTV 再作成。
// Frame:  対象 HWND が一致するとき、RTV へ bind・viewport・clear →（T33）D2D 1 行 → Present（GDI は呼ばない）。
bool WindowsRenderer_Init(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState);
void WindowsRenderer_Shutdown(WindowsRendererState* state);
void WindowsRenderer_Resize(WindowsRendererState* state, UINT32 clientW, UINT32 clientH);
void WindowsRenderer_Frame(WindowsRendererState* state, HWND hwnd);
