// T23/T24/T25: Windows 描画・DirectX 11 最小レンダラ
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

// T24 lifecycle 名を維持（中身は T25 で D3D11 初期化・描画）
bool WindowsRenderer_InitPlaceholder(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState);
void WindowsRenderer_ShutdownPlaceholder(WindowsRendererState* state);
void WindowsRenderer_OnResizePlaceholder(WindowsRendererState* state, UINT32 clientW, UINT32 clientH);
void WindowsRenderer_RenderPlaceholder(WindowsRendererState* state, HWND hwnd);
