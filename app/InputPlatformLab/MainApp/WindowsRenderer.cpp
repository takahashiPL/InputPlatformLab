#include "WindowsRenderer.h"

#include <algorithm>
#include <stdio.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
bool s_loggedPresentOk = false;

bool WindowsRenderer_InternalCreateRtv(WindowsRendererState* s)
{
    if (!s || !s->swapChain || !s->device)
    {
        return false;
    }
    if (s->rtv)
    {
        s->rtv->Release();
        s->rtv = nullptr;
    }
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = s->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || backBuffer == nullptr)
    {
        wchar_t line[128] = {};
        swprintf_s(
            line,
            L"[D3D11] rtv GetBuffer fail hr=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        return false;
    }
    hr = s->device->CreateRenderTargetView(backBuffer, nullptr, &s->rtv);
    backBuffer->Release();
    if (FAILED(hr) || s->rtv == nullptr)
    {
        wchar_t line[128] = {};
        swprintf_s(
            line,
            L"[D3D11] rtv CreateRenderTargetView fail hr=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        return false;
    }
    return true;
}
} // namespace

bool WindowsRenderer_InitPlaceholder(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState)
{
    if (!outState || !hwnd)
    {
        OutputDebugStringW(L"[D3D11] init fail (bad hwnd or state)\r\n");
        return false;
    }
    WindowsRenderer_ShutdownPlaceholder(outState);

    const std::uint32_t w = (std::max)(1u, cfg.clientWidth);
    const std::uint32_t h = (std::max)(1u, cfg.clientHeight);

    OutputDebugStringW(L"[D3D11] init: D3D11CreateDeviceAndSwapChain (loads d3d11.dll / dxgi.dll on demand)\r\n");

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = w;
    sd.BufferDesc.Height = h;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 0;
    sd.BufferDesc.RefreshRate.Denominator = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    UINT createFlags = 0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
        D3D11_SDK_VERSION,
        &sd,
        &outState->swapChain,
        &outState->device,
        &obtained,
        &outState->context);
    if (FAILED(hr) || outState->device == nullptr || outState->context == nullptr || outState->swapChain == nullptr)
    {
        wchar_t line[160] = {};
        swprintf_s(
            line,
            L"[D3D11] init fail D3D11CreateDeviceAndSwapChain hr=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        WindowsRenderer_ShutdownPlaceholder(outState);
        return false;
    }

    outState->targetHwnd = hwnd;
    outState->clientWidth = w;
    outState->clientHeight = h;

    if (!WindowsRenderer_InternalCreateRtv(outState))
    {
        OutputDebugStringW(L"[D3D11] init fail (CreateRtv)\r\n");
        WindowsRenderer_ShutdownPlaceholder(outState);
        return false;
    }

    outState->initialized = true;
    {
        wchar_t line[192] = {};
        swprintf_s(
            line,
            L"[D3D11] init ok %ux%u featureLevel=0x%04X\r\n",
            static_cast<unsigned int>(w),
            static_cast<unsigned int>(h),
            static_cast<unsigned int>(obtained));
        OutputDebugStringW(line);
    }
    return true;
}

void WindowsRenderer_ShutdownPlaceholder(WindowsRendererState* state)
{
    if (!state)
    {
        return;
    }
    if (state->context)
    {
        state->context->ClearState();
        state->context->Flush();
    }
    if (state->rtv)
    {
        state->rtv->Release();
        state->rtv = nullptr;
    }
    if (state->swapChain)
    {
        state->swapChain->Release();
        state->swapChain = nullptr;
    }
    if (state->context)
    {
        state->context->Release();
        state->context = nullptr;
    }
    if (state->device)
    {
        state->device->Release();
        state->device = nullptr;
    }
    state->initialized = false;
    state->targetHwnd = nullptr;
    state->clientWidth = 0;
    state->clientHeight = 0;
    s_loggedPresentOk = false;
}

void WindowsRenderer_OnResizePlaceholder(WindowsRendererState* state, std::uint32_t clientW, std::uint32_t clientH)
{
    if (!state || !state->initialized || !state->swapChain || !state->device)
    {
        return;
    }
    state->clientWidth = clientW;
    state->clientHeight = clientH;
    if (clientW == 0 || clientH == 0)
    {
        if (state->rtv)
        {
            state->rtv->Release();
            state->rtv = nullptr;
        }
        return;
    }

    if (state->rtv)
    {
        state->rtv->Release();
        state->rtv = nullptr;
    }
    HRESULT hr = state->swapChain->ResizeBuffers(0, clientW, clientH, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        wchar_t line[160] = {};
        swprintf_s(
            line,
            L"[D3D11] resize fail ResizeBuffers hr=0x%08X %ux%u\r\n",
            static_cast<unsigned int>(hr),
            static_cast<unsigned int>(clientW),
            static_cast<unsigned int>(clientH));
        OutputDebugStringW(line);
        return;
    }
    if (!WindowsRenderer_InternalCreateRtv(state))
    {
        wchar_t line[128] = {};
        swprintf_s(
            line,
            L"[D3D11] resize fail CreateRtv %ux%u\r\n",
            static_cast<unsigned int>(clientW),
            static_cast<unsigned int>(clientH));
        OutputDebugStringW(line);
        return;
    }
    {
        wchar_t line[128] = {};
        swprintf_s(
            line,
            L"[D3D11] resize ok %ux%u\r\n",
            static_cast<unsigned int>(clientW),
            static_cast<unsigned int>(clientH));
        OutputDebugStringW(line);
    }
}

void WindowsRenderer_RenderPlaceholder(WindowsRendererState* state, HWND hwnd)
{
    if (!state || !state->initialized || !state->context || !state->swapChain || !state->rtv)
    {
        return;
    }
    if (hwnd != state->targetHwnd)
    {
        return;
    }
    if (state->clientWidth == 0 || state->clientHeight == 0)
    {
        return;
    }

    ID3D11RenderTargetView* rtv = state->rtv;
    state->context->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(state->clientWidth);
    vp.Height = static_cast<float>(state->clientHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    state->context->RSSetViewports(1, &vp);

    const float clearColor[4] = { 0.12f, 0.14f, 0.18f, 1.0f };
    state->context->ClearRenderTargetView(state->rtv, clearColor);
    const HRESULT hrPresent = state->swapChain->Present(1, 0);
    if (FAILED(hrPresent))
    {
        wchar_t line[128] = {};
        swprintf_s(
            line,
            L"[D3D11] present fail hr=0x%08X\r\n",
            static_cast<unsigned int>(hrPresent));
        OutputDebugStringW(line);
    }
    else
    {
        if (!s_loggedPresentOk)
        {
            s_loggedPresentOk = true;
            OutputDebugStringW(L"[D3D11] present ok (first)\r\n");
        }
    }
}
