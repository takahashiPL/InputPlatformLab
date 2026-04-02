// T23/T24/T25: Windows 描画・DirectX 11 最小レンダラ
//
// MainApp の GDI デバッグ描画は WM_PAINT 内で D3D clear/present の後に実行（上書き合成）。
#pragma once

#include "framework.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

struct WindowsRendererConfig
{
    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
};

struct WindowsRendererState
{
    bool initialized = false;
    HWND targetHwnd = nullptr;
    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};

// T24 lifecycle 名を維持（中身は T25 で D3D11 初期化・描画）
bool WindowsRenderer_InitPlaceholder(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState);
void WindowsRenderer_ShutdownPlaceholder(WindowsRendererState* state);
void WindowsRenderer_OnResizePlaceholder(WindowsRendererState* state, std::uint32_t clientW, std::uint32_t clientH);
void WindowsRenderer_RenderPlaceholder(WindowsRendererState* state, HWND hwnd);
