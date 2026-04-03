// T23/T24/T25: Windows 描画・DirectX 11 最小レンダラ
// T31: 公開面は Init / Resize / Frame の 3 役割（+ ライフサイクル Shutdown）。
//
// MainApp の GDI デバッグ描画は WM_PAINT 内で D3D clear/present の後に実行（上書き合成）。
// D3D は背景クリアのみ。長文テキスト・スクロールは Win32DebugOverlay（GDI）。
#pragma once

#include "CommonTypes.h"
#include "framework.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

// D3D 初期化時のクライアントサイズ（物理ピクセル想定）
struct WindowsRendererConfig
{
    UINT32 clientWidth = 0;
    UINT32 clientHeight = 0;
};

// スワップチェーンと RTV を保持。描画本体はプレースホルダ（クリア + Present）。
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
};

// --- T31 公開面（3 入口 + Shutdown）---
// Init:   デバイス・スワップチェーン・初回 RTV。DLL はこの呼び出しでロードされる。
// Resize: WM_SIZE 相当のクライアント幅・高さ → ResizeBuffers + RTV 再作成。
// Frame:  対象 HWND が一致するとき、RTV へ bind・viewport・clear・Present（GDI は呼ばない）。
bool WindowsRenderer_Init(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState);
void WindowsRenderer_Shutdown(WindowsRendererState* state);
void WindowsRenderer_Resize(WindowsRendererState* state, UINT32 clientW, UINT32 clientH);
void WindowsRenderer_Frame(WindowsRendererState* state, HWND hwnd);
