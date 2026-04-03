#include "WindowsRenderer.h"

#include <algorithm>
#include <d2derr.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

// ---------------------------------------------------------------------------
// D3D11（T31）: Init / Resize / Frame。T33: clear → D2D/DWrite 1 行 → Present 1 回。
// GDI・BeginPaint/EndPaint は MainApp（Win32_MainView_PaintFrame）側。
// ---------------------------------------------------------------------------

namespace
{
bool s_loggedPresentOk = false;
bool s_loggedT33ResizeNote = false;
bool s_t33FirstFrameDiagDone = false;

static void WindowsRenderer_InternalShutdownD2D(WindowsRendererState* s);

static void T33_LogInitOk(const wchar_t* stepName, HRESULT hr)
{
    wchar_t line[192] = {};
    swprintf_s(
        line,
        L"[T33] init: %s hr=0x%08X\r\n",
        stepName,
        static_cast<unsigned int>(hr));
    OutputDebugStringW(line);
}

static bool T33_LogInitFail(const wchar_t* apiName, HRESULT hr, WindowsRendererState* s)
{
    wchar_t line[224] = {};
    swprintf_s(
        line,
        L"[T33] fail: %s hr=0x%08X -> D3D clear+Present only\r\n",
        apiName,
        static_cast<unsigned int>(hr));
    OutputDebugStringW(line);
    WindowsRenderer_InternalShutdownD2D(s);
    return false;
}

static void T33_LogFrameStep(const wchar_t* stepName, HRESULT hr)
{
    wchar_t line[192] = {};
    swprintf_s(
        line,
        L"[T33] frame: %s hr=0x%08X\r\n",
        stepName,
        static_cast<unsigned int>(hr));
    OutputDebugStringW(line);
}

static void WindowsRenderer_InternalReleaseRtv(WindowsRendererState* s)
{
    if (!s || !s->rtv)
    {
        return;
    }
    s->rtv->Release();
    s->rtv = nullptr;
}

bool WindowsRenderer_InternalCreateRtv(WindowsRendererState* s)
{
    if (!s || !s->swapChain || !s->device)
    {
        return false;
    }
    WindowsRenderer_InternalReleaseRtv(s);
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

static void WindowsRenderer_InternalShutdownD2D(WindowsRendererState* s)
{
    if (!s)
    {
        return;
    }
    if (s->d2dTextBrush)
    {
        s->d2dTextBrush->Release();
        s->d2dTextBrush = nullptr;
    }
    if (s->dwriteTextFormat)
    {
        s->dwriteTextFormat->Release();
        s->dwriteTextFormat = nullptr;
    }
    if (s->d2dContext)
    {
        s->d2dContext->SetTarget(nullptr);
        s->d2dContext->Release();
        s->d2dContext = nullptr;
    }
    if (s->d2dDevice)
    {
        s->d2dDevice->Release();
        s->d2dDevice = nullptr;
    }
    if (s->dwriteFactory)
    {
        s->dwriteFactory->Release();
        s->dwriteFactory = nullptr;
    }
    if (s->d2dFactory)
    {
        s->d2dFactory->Release();
        s->d2dFactory = nullptr;
    }
}

static bool WindowsRenderer_InternalInitD2D(WindowsRendererState* s)
{
    if (!s || !s->device)
    {
        return false;
    }
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1),
        nullptr,
        reinterpret_cast<void**>(&s->d2dFactory));
    if (FAILED(hr) || s->d2dFactory == nullptr)
    {
        return T33_LogInitFail(L"D2D1CreateFactory(ID2D1Factory1)", hr, s);
    }
    T33_LogInitOk(L"D2D1CreateFactory", hr);

    IDXGIDevice* dxgiDevice = nullptr;
    hr = s->device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr) || dxgiDevice == nullptr)
    {
        return T33_LogInitFail(L"IUnknown::QueryInterface(IDXGIDevice)", hr, s);
    }
    T33_LogInitOk(L"QueryInterface(IDXGIDevice)", hr);

    hr = s->d2dFactory->CreateDevice(dxgiDevice, &s->d2dDevice);
    dxgiDevice->Release();
    dxgiDevice = nullptr;
    if (FAILED(hr) || s->d2dDevice == nullptr)
    {
        return T33_LogInitFail(L"ID2D1Factory1::CreateDevice", hr, s);
    }
    T33_LogInitOk(L"ID2D1Factory1::CreateDevice", hr);

    hr = s->d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &s->d2dContext);
    if (FAILED(hr) || s->d2dContext == nullptr)
    {
        return T33_LogInitFail(L"ID2D1Device::CreateDeviceContext", hr, s);
    }
    T33_LogInitOk(L"ID2D1Device::CreateDeviceContext", hr);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&s->dwriteFactory));
    if (FAILED(hr) || s->dwriteFactory == nullptr)
    {
        return T33_LogInitFail(L"DWriteCreateFactory", hr, s);
    }
    T33_LogInitOk(L"DWriteCreateFactory", hr);

    hr = s->dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        16.0f,
        L"ja-jp",
        &s->dwriteTextFormat);
    if (FAILED(hr) || s->dwriteTextFormat == nullptr)
    {
        return T33_LogInitFail(L"IDWriteFactory::CreateTextFormat", hr, s);
    }
    T33_LogInitOk(L"IDWriteFactory::CreateTextFormat", hr);

    hr = s->d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &s->d2dTextBrush);
    if (FAILED(hr) || s->d2dTextBrush == nullptr)
    {
        return T33_LogInitFail(L"ID2D1DeviceContext::CreateSolidColorBrush", hr, s);
    }
    T33_LogInitOk(L"ID2D1DeviceContext::CreateSolidColorBrush", hr);

    return true;
}

static void WindowsRenderer_InternalDrawD2DOneLine(WindowsRendererState* s)
{
    if (!s->d2dContext || !s->dwriteTextFormat || !s->d2dTextBrush || !s->swapChain || !s->d2dFactory)
    {
        return;
    }
    const bool firstDiag = !s_t33FirstFrameDiagDone;
    struct T33FirstFrameDiagGuard
    {
        bool armed;
        ~T33FirstFrameDiagGuard()
        {
            if (armed)
            {
                s_t33FirstFrameDiagDone = true;
            }
        }
    } diagGuard{ firstDiag };

    auto logFrame = [&](const wchar_t* step, HRESULT stepHr) {
        if (firstDiag)
        {
            T33_LogFrameStep(step, stepHr);
        }
    };

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = s->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    logFrame(L"IDXGISwapChain::GetBuffer(0)", hr);
    if (FAILED(hr) || backBuffer == nullptr)
    {
        return;
    }
    IDXGISurface* surface = nullptr;
    hr = backBuffer->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&surface));
    backBuffer->Release();
    logFrame(L"ID3D11Texture2D::QueryInterface(IDXGISurface)", hr);
    if (FAILED(hr) || surface == nullptr)
    {
        return;
    }

    UINT dpiSys = GetDpiForWindow(s->targetHwnd);
    if (dpiSys == 0)
    {
        dpiSys = 96u;
    }
    const FLOAT dpiX = static_cast<FLOAT>(dpiSys);
    const FLOAT dpiY = static_cast<FLOAT>(dpiSys);

    const D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiX,
        dpiY);

    ID2D1Bitmap1* target = nullptr;
    hr = s->d2dContext->CreateBitmapFromDxgiSurface(surface, &bmpProps, &target);
    surface->Release();
    logFrame(L"ID2D1DeviceContext::CreateBitmapFromDxgiSurface", hr);
    if (FAILED(hr) || target == nullptr)
    {
        return;
    }

    s->d2dContext->SetTarget(target);
    s->d2dContext->BeginDraw();
    if (firstDiag)
    {
        T33_LogFrameStep(L"ID2D1DeviceContext::BeginDraw", S_OK);
    }
    s->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());

    static const wchar_t kLine[] = L"T33: DirectWrite overlay (1 line)";
    const UINT32 kLen = static_cast<UINT32>(wcslen(kLine));
    const float w = static_cast<float>(s->clientWidth);
    const float h = static_cast<float>(s->clientHeight);
    const D2D1_RECT_F layout = D2D1::RectF(8.0f, 8.0f, (std::max)(8.0f, w - 8.0f), (std::max)(8.0f, h - 8.0f));

    s->d2dContext->DrawText(
        kLine,
        kLen,
        s->dwriteTextFormat,
        layout,
        s->d2dTextBrush,
        D2D1_DRAW_TEXT_OPTIONS_NONE,
        DWRITE_MEASURING_MODE_NATURAL);
    if (firstDiag)
    {
        T33_LogFrameStep(L"ID2D1DeviceContext::DrawText", S_OK);
    }

    hr = s->d2dContext->EndDraw();
    logFrame(L"ID2D1DeviceContext::EndDraw", hr);
    target->Release();
    s->d2dContext->SetTarget(nullptr);

    static bool s_loggedFirstEndDrawOk = false;
    if (SUCCEEDED(hr) && !s_loggedFirstEndDrawOk)
    {
        s_loggedFirstEndDrawOk = true;
        OutputDebugStringW(L"[T33] first EndDraw ok\r\n");
    }
    else if (FAILED(hr) && hr != D2DERR_RECREATE_TARGET && !firstDiag)
    {
        wchar_t line[128] = {};
        swprintf_s(line, L"[T33] EndDraw hr=0x%08X\r\n", static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
    }
}

// clear（D3D）→ flush → D2D 1 行（任意）→ Present 1 回
static void WindowsRenderer_Internal_ClearD2DPresent(WindowsRendererState* state)
{
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
    state->context->Flush();

    if (state->d2dContext != nullptr)
    {
        WindowsRenderer_InternalDrawD2DOneLine(state);
    }

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
} // namespace

// Init: デバイス・スワップチェーン・RTV を作成。DLL はこの呼び出しでロードされる。
bool WindowsRenderer_Init(HWND hwnd, const WindowsRendererConfig& cfg, WindowsRendererState* outState)
{
    if (!outState || !hwnd)
    {
        OutputDebugStringW(L"[D3D11] init fail (bad hwnd or state)\r\n");
        return false;
    }
    WindowsRenderer_Shutdown(outState);

    const UINT32 w = (std::max)(1u, cfg.clientWidth);
    const UINT32 h = (std::max)(1u, cfg.clientHeight);

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
    const UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

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
        WindowsRenderer_Shutdown(outState);
        return false;
    }

    outState->targetHwnd = hwnd;
    outState->clientWidth = w;
    outState->clientHeight = h;

    {
        wchar_t line[224] = {};
        swprintf_s(
            line,
            L"[D3D11] D3D11CreateDeviceAndSwapChain flags=0x%08X BGRA_SUPPORT=%d\r\n",
            static_cast<unsigned int>(createFlags),
            (createFlags & D3D11_CREATE_DEVICE_BGRA_SUPPORT) != 0 ? 1 : 0);
        OutputDebugStringW(line);
    }
    {
        DXGI_SWAP_CHAIN_DESC scDesc = {};
        if (SUCCEEDED(outState->swapChain->GetDesc(&scDesc)))
        {
            const DXGI_FORMAT fmt = scDesc.BufferDesc.Format;
            const bool fmtOkForD2D =
                (fmt == DXGI_FORMAT_R8G8B8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM);
            wchar_t line[288] = {};
            swprintf_s(
                line,
                L"[D3D11] swapchain BufferDesc.Format=%u (R8G8B8A8_UNORM=%u; typical D2D DXGI interop)\r\n",
                static_cast<unsigned int>(fmt),
                static_cast<unsigned int>(DXGI_FORMAT_R8G8B8A8_UNORM));
            OutputDebugStringW(line);
            if (!fmtOkForD2D)
            {
                OutputDebugStringW(
                    L"[D3D11] warn: backbuffer format is not R8G8B8A8/B8G8R8A8 UNORM; D2D DXGI surface may fail\r\n");
            }
        }
    }

    if (!WindowsRenderer_InternalCreateRtv(outState))
    {
        OutputDebugStringW(L"[D3D11] init fail (CreateRtv)\r\n");
        WindowsRenderer_Shutdown(outState);
        return false;
    }

    if (WindowsRenderer_InternalInitD2D(outState))
    {
        OutputDebugStringW(L"[T33] D2D/DWrite ok\r\n");
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

// Shutdown: COM 参照を解放し、状態を無効化。
void WindowsRenderer_Shutdown(WindowsRendererState* state)
{
    if (!state)
    {
        return;
    }
    WindowsRenderer_InternalShutdownD2D(state);
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
    s_loggedT33ResizeNote = false;
    s_t33FirstFrameDiagDone = false;
}

// Resize: WM_SIZE に合わせてバックバッファを ResizeBuffers し、RTV を作り直す。
void WindowsRenderer_Resize(WindowsRendererState* state, UINT32 clientW, UINT32 clientH)
{
    if (!state || !state->initialized || !state->swapChain || !state->device)
    {
        return;
    }
    state->clientWidth = clientW;
    state->clientHeight = clientH;
    if (clientW == 0 || clientH == 0)
    {
        WindowsRenderer_InternalReleaseRtv(state);
        return;
    }

    WindowsRenderer_InternalReleaseRtv(state);
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
    if (state->d2dContext != nullptr && !s_loggedT33ResizeNote)
    {
        s_loggedT33ResizeNote = true;
        OutputDebugStringW(L"[T33] after resize: D2D draws per-frame DXGI surface (no D2D device recreate)\r\n");
    }
}

// Frame: RTV へ bind・viewport・clear → D2D 1 行（成功時）→ Present。
void WindowsRenderer_Frame(WindowsRendererState* state, HWND hwnd)
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

    WindowsRenderer_Internal_ClearD2DPresent(state);
}
