#include "WindowsRenderer.h"

bool WindowsRenderer_InitPlaceholder(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState)
{
    if (!outState || !hwnd)
    {
        return false;
    }
    *outState = {};
    outState->initialized = true;
    outState->targetHwnd = hwnd;
    outState->clientWidth = cfg.clientWidth;
    outState->clientHeight = cfg.clientHeight;
    return true;
}

void WindowsRenderer_ShutdownPlaceholder(WindowsRendererState* state)
{
    if (!state)
    {
        return;
    }
    *state = {};
}

void WindowsRenderer_OnResizePlaceholder(WindowsRendererState* state, std::uint32_t clientW, std::uint32_t clientH)
{
    if (!state || !state->initialized)
    {
        return;
    }
    state->clientWidth = clientW;
    state->clientHeight = clientH;
}

void WindowsRenderer_RenderPlaceholder(const WindowsRendererState* state, HWND hwnd)
{
    (void)state;
    (void)hwnd;
    // T24: 将来 D3D フレーム描画。現状 no-op（GDI は MainApp の WM_PAINT 側）。
}
