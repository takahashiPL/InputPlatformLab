// T23/T24: Windows 描画・将来 DirectX レンダラ受け皿
//
// 現状は GDI デバッグ描画が MainApp.cpp。将来 DXGI/D3D はここへ集約（T24: placeholder API のみ）。
// d3d11.h / dxgi.h はまだ include しない。
#pragma once

#include "framework.h"

#include <cstdint>

// レンダラ作成時の希望（将来: MSAA / 垂直同期フラグ等を追加）
struct WindowsRendererConfig
{
    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
};

// ランタイム状態（将来: device / swapchain ポインタを不透明に保持）
struct WindowsRendererState
{
    bool initialized = false;
    HWND targetHwnd = nullptr;
    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
};

// T24: DirectX 導入前のプレースホルダー（no-op）。失敗時のみ false。
bool WindowsRenderer_InitPlaceholder(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState);
void WindowsRenderer_ShutdownPlaceholder(WindowsRendererState* state);
void WindowsRenderer_OnResizePlaceholder(WindowsRendererState* state, std::uint32_t clientW, std::uint32_t clientH);
void WindowsRenderer_RenderPlaceholder(const WindowsRendererState* state, HWND hwnd);
