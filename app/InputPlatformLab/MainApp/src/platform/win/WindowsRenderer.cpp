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
// D3D11（T31）: Init / Resize / Frame。T33: clear →（任意）可変グリッド → D2D/DWrite 1 行 → Present 1 回。
// T34（完了）: Borderless のみ — committed 解像度のオフスクリーン RT にグリッド/T33 を描き、swapchain バックバッファへ拡大合成（ログ [T34][RT]）。
// T35: 3 モードの window/client・swapchain・offscreen・Present・GDI の正式方針は docs/t35_display_mode_policy.md（T17 ログとは別軸）。
// T36: Fullscreen のみ committed オフスクリーン実験（ログ [T36][RT]）。Borderless と T34 は別リソース。
// グリッド: cell=(1280*64)/denomPhysW。denom は MainApp が毎フレーム設定（既定: T14 Enter 確定幅。無ければ client 幅）。
// GDI・BeginPaint/EndPaint は MainApp（Win32_MainView_PaintFrame）側。
// ---------------------------------------------------------------------------

// [GRID] mode=... の 1 回ログ用（MainApp の WindowsRenderer_DebugGrid_ResetLogOnce と共有）
bool s_loggedGridOnce = false;

// T34: Borderless 実験用オフスクリーン（T17 のウィンドウサイズログとは別軸）
static void WindowsRenderer_ReleaseBorderlessOffscreen(WindowsRendererState* s)
{
    if (!s)
    {
        return;
    }
    if (s->borderlessOffscreenRtv)
    {
        s->borderlessOffscreenRtv->Release();
        s->borderlessOffscreenRtv = nullptr;
    }
    if (s->borderlessOffscreenTexture)
    {
        s->borderlessOffscreenTexture->Release();
        s->borderlessOffscreenTexture = nullptr;
    }
}

void WindowsRenderer_ClearBorderlessOffscreen(WindowsRendererState* s)
{
    if (!s)
    {
        return;
    }
    WindowsRenderer_ReleaseBorderlessOffscreen(s);
    s->borderlessOffscreenComposite = false;
    s->borderlessOffscreenPhysW = 0;
    s->borderlessOffscreenPhysH = 0;
}

static void WindowsRenderer_ReleaseFullscreenOffscreen(WindowsRendererState* s)
{
    if (!s)
    {
        return;
    }
    if (s->fullscreenOffscreenRtv)
    {
        s->fullscreenOffscreenRtv->Release();
        s->fullscreenOffscreenRtv = nullptr;
    }
    if (s->fullscreenOffscreenTexture)
    {
        s->fullscreenOffscreenTexture->Release();
        s->fullscreenOffscreenTexture = nullptr;
    }
}

void WindowsRenderer_ClearFullscreenOffscreen(WindowsRendererState* s)
{
    if (!s)
    {
        return;
    }
    WindowsRenderer_ReleaseFullscreenOffscreen(s);
    s->fullscreenOffscreenComposite = false;
    s->fullscreenOffscreenPhysW = 0;
    s->fullscreenOffscreenPhysH = 0;
}

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
    if (s->d2dGridBrushEm)
    {
        s->d2dGridBrushEm->Release();
        s->d2dGridBrushEm = nullptr;
    }
    if (s->d2dGridBrush)
    {
        s->d2dGridBrush->Release();
        s->d2dGridBrush = nullptr;
    }
    if (s->dwriteGridLabelFormat)
    {
        s->dwriteGridLabelFormat->Release();
        s->dwriteGridLabelFormat = nullptr;
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

#if WIN32_RENDERER_DEBUG_GRID_64PX
    hr = s->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.44f, 0.52f, 0.50f), &s->d2dGridBrush);
    if (FAILED(hr))
    {
        s->d2dGridBrush = nullptr;
    }
    hr = s->d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.62f, 0.66f, 0.74f, 0.75f), &s->d2dGridBrushEm);
    if (FAILED(hr))
    {
        s->d2dGridBrushEm = nullptr;
    }
    if (s->dwriteFactory)
    {
        hr = s->dwriteFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            10.0f,
            L"ja-jp",
            &s->dwriteGridLabelFormat);
        if (FAILED(hr))
        {
            s->dwriteGridLabelFormat = nullptr;
        }
    }
#endif

    return true;
}

#if WIN32_RENDERER_DEBUG_GRID_64PX
// cell = (1280*64) / denomPhysW。denom は MainApp が設定: Enter 確定の T14 選択幅、Client = 未確定フォールバック。
static UINT WindowsRenderer_ComputeDebugGridStepPx(const WindowsRendererState* s)
{
    const UINT cw = s->clientWidth;
    if (cw < 1u)
    {
        return 4u;
    }
    static constexpr UINT kRefClientW = 1280u;
    static constexpr UINT kRefCellPx = 64u;
    UINT denom = s->gridDebugDenomPhysW;
    if (denom < 1u)
    {
        denom = cw;
    }
    return (std::max)(4u, (kRefClientW * kRefCellPx) / (std::max)(1u, denom));
}

static const wchar_t* WindowsRenderer_GridBasisLabel(WindowsRendererGridDebugBasis b)
{
    switch (b)
    {
    case WindowsRendererGridDebugBasis::CommittedSelected:
        return L"committedSelected";
    case WindowsRendererGridDebugBasis::Client:
    default:
        return L"client";
    }
}

static void WindowsRenderer_InternalDrawDebugGridLines(
    WindowsRendererState* s,
    UINT dpiSys,
    float sx,
    float sy,
    float wDip,
    float hDip,
    UINT stepPx)
{
    if (!s->d2dContext || !s->d2dGridBrush || !s->d2dGridBrushEm)
    {
        return;
    }
    const UINT cw = s->clientWidth;
    const UINT ch = s->clientHeight;
    if (cw < 1u || ch < 1u || stepPx < 1u)
    {
        return;
    }

    s->d2dContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    for (UINT x = 0; x <= cw; x += stepPx)
    {
        const int ix = static_cast<int>(x / stepPx);
        ID2D1SolidColorBrush* br = ((ix % 8) == 0) ? s->d2dGridBrushEm : s->d2dGridBrush;
        const float stroke = ((ix % 8) == 0) ? 1.5f : 1.0f;
        const float xd = static_cast<float>(x) * sx;
        s->d2dContext->DrawLine(
            D2D1::Point2F(xd, 0.f),
            D2D1::Point2F(xd, hDip),
            br,
            stroke,
            nullptr);
    }
    for (UINT y = 0; y <= ch; y += stepPx)
    {
        const int iy = static_cast<int>(y / stepPx);
        ID2D1SolidColorBrush* br = ((iy % 8) == 0) ? s->d2dGridBrushEm : s->d2dGridBrush;
        const float stroke = ((iy % 8) == 0) ? 1.5f : 1.0f;
        const float yd = static_cast<float>(y) * sy;
        s->d2dContext->DrawLine(
            D2D1::Point2F(0.f, yd),
            D2D1::Point2F(wDip, yd),
            br,
            stroke,
            nullptr);
    }

    s->d2dContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (!s_loggedGridOnce)
    {
        s_loggedGridOnce = true;
        const UINT denom = (s->gridDebugDenomPhysW >= 1u) ? s->gridDebugDenomPhysW : cw;
        wchar_t line[384] = {};
        swprintf_s(
            line,
            _countof(line),
            L"[GRID] mode=scaleRef1280x64 basis=%s denomPhysW=%u cellPx=%u client=%ux%u dpi=%u\r\n",
            WindowsRenderer_GridBasisLabel(s->gridDebugBasis),
            static_cast<unsigned int>(denom),
            static_cast<unsigned int>(stepPx),
            static_cast<unsigned int>(cw),
            static_cast<unsigned int>(ch),
            static_cast<unsigned int>(dpiSys));
        OutputDebugStringW(line);
    }
}

static void WindowsRenderer_InternalDrawDebugGridLabels(
    WindowsRendererState* s,
    float wDip,
    float hDip,
    UINT stepPx)
{
    if (!s->d2dContext || !s->dwriteGridLabelFormat || !s->d2dGridBrushEm)
    {
        return;
    }
    wchar_t cap[512] = {};
    if (s->gridDebugCommittedPhysW > 0u && s->gridDebugCommittedPhysH > 0u)
    {
        swprintf_s(
            cap,
            _countof(cap),
            L"basis=%s\r\n"
            L"committed=%ux%u\r\n"
            L"client=%ux%u\r\n"
            L"cellPx=%u",
            WindowsRenderer_GridBasisLabel(s->gridDebugBasis),
            static_cast<unsigned int>(s->gridDebugCommittedPhysW),
            static_cast<unsigned int>(s->gridDebugCommittedPhysH),
            static_cast<unsigned int>(s->gridDebugClientPhysW),
            static_cast<unsigned int>(s->gridDebugClientPhysH),
            static_cast<unsigned int>(stepPx));
    }
    else
    {
        swprintf_s(
            cap,
            _countof(cap),
            L"basis=%s\r\n"
            L"committed=(n/a)\r\n"
            L"client=%ux%u\r\n"
            L"cellPx=%u",
            WindowsRenderer_GridBasisLabel(s->gridDebugBasis),
            static_cast<unsigned int>(s->gridDebugClientPhysW),
            static_cast<unsigned int>(s->gridDebugClientPhysH),
            static_cast<unsigned int>(stepPx));
    }

    // 左上（4 行）。T33 はこの下にオフセットして描画する。
    const float topH = 72.0f;
    const D2D1_RECT_F layoutTop = D2D1::RectF(4.0f, 4.0f, wDip - 4.0f, (std::min)(topH, hDip - 4.0f));
    s->d2dContext->DrawText(
        cap,
        static_cast<UINT32>(wcslen(cap)),
        s->dwriteGridLabelFormat,
        layoutTop,
        s->d2dGridBrushEm,
        D2D1_DRAW_TEXT_OPTIONS_NONE,
        DWRITE_MEASURING_MODE_NATURAL);
}
#endif

// surface は参照を 1 つ消費する（CreateBitmapFromDxgiSurface 成功後に Release）
static HRESULT WindowsRenderer_DrawD2DContentToSurface(
    WindowsRendererState* s,
    IDXGISurface* surface,
    UINT pixelW,
    UINT pixelH,
    bool firstDiag)
{
    if (!s->d2dContext || !surface || pixelW < 1u || pixelH < 1u)
    {
        if (surface)
        {
            surface->Release();
        }
        return E_INVALIDARG;
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
    HRESULT hr = s->d2dContext->CreateBitmapFromDxgiSurface(surface, &bmpProps, &target);
    surface->Release();
    if (firstDiag)
    {
        T33_LogFrameStep(L"ID2D1DeviceContext::CreateBitmapFromDxgiSurface", hr);
    }
    if (FAILED(hr) || target == nullptr)
    {
        return hr;
    }

    const UINT saveW = s->clientWidth;
    const UINT saveH = s->clientHeight;
    s->clientWidth = pixelW;
    s->clientHeight = pixelH;

    s->d2dContext->SetTarget(target);
    s->d2dContext->BeginDraw();
    if (firstDiag)
    {
        T33_LogFrameStep(L"ID2D1DeviceContext::BeginDraw", S_OK);
    }
    s->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());

    const float sx = 96.f / dpiX;
    const float sy = 96.f / dpiY;
    const float wDip = static_cast<float>(pixelW) * sx;
    const float hDip = static_cast<float>(pixelH) * sy;
#if WIN32_RENDERER_DEBUG_GRID_64PX
    const UINT gridStepPx = WindowsRenderer_ComputeDebugGridStepPx(s);
    WindowsRenderer_InternalDrawDebugGridLines(s, dpiSys, sx, sy, wDip, hDip, gridStepPx);
    WindowsRenderer_InternalDrawDebugGridLabels(s, wDip, hDip, gridStepPx);
#endif

    static const wchar_t kLine[] = L"T33: DirectWrite overlay (1 line)";
    const UINT32 kLen = static_cast<UINT32>(wcslen(kLine));
    const float w = static_cast<float>(pixelW);
    const float h = static_cast<float>(pixelH);
#if WIN32_RENDERER_DEBUG_GRID_64PX
    const float t33Top = 80.0f;
#else
    const float t33Top = 8.0f;
#endif
    const D2D1_RECT_F layout = D2D1::RectF(
        8.0f,
        t33Top,
        (std::max)(8.0f, w - 8.0f),
        (std::max)(t33Top + 4.0f, h - 8.0f));

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
    if (firstDiag)
    {
        T33_LogFrameStep(L"ID2D1DeviceContext::EndDraw", hr);
    }
    target->Release();
    s->d2dContext->SetTarget(nullptr);

    s->clientWidth = saveW;
    s->clientHeight = saveH;

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
    return hr;
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

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = s->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (firstDiag)
    {
        T33_LogFrameStep(L"IDXGISwapChain::GetBuffer(0)", hr);
    }
    if (FAILED(hr) || backBuffer == nullptr)
    {
        return;
    }
    IDXGISurface* surface = nullptr;
    hr = backBuffer->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&surface));
    backBuffer->Release();
    if (firstDiag)
    {
        T33_LogFrameStep(L"ID3D11Texture2D::QueryInterface(IDXGISurface)", hr);
    }
    if (FAILED(hr) || surface == nullptr)
    {
        return;
    }

    (void)WindowsRenderer_DrawD2DContentToSurface(s, surface, s->clientWidth, s->clientHeight, firstDiag);
}

static bool WindowsRenderer_InternalEnsureBorderlessOffscreenRT(WindowsRendererState* s, UINT w, UINT h)
{
    if (!s || !s->device || w < 1u || h < 1u)
    {
        return false;
    }
    if (s->borderlessOffscreenTexture != nullptr)
    {
        D3D11_TEXTURE2D_DESC td = {};
        s->borderlessOffscreenTexture->GetDesc(&td);
        if (td.Width == w && td.Height == h)
        {
            return true;
        }
    }
    WindowsRenderer_ReleaseBorderlessOffscreen(s);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = 0;
    td.MiscFlags = 0;

    HRESULT hr = s->device->CreateTexture2D(&td, nullptr, &s->borderlessOffscreenTexture);
    if (FAILED(hr) || s->borderlessOffscreenTexture == nullptr)
    {
        wchar_t line[192] = {};
        swprintf_s(
            line,
            L"[T34][RT] offscreen create FAILED hr=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        return false;
    }
    hr = s->device->CreateRenderTargetView(s->borderlessOffscreenTexture, nullptr, &s->borderlessOffscreenRtv);
    if (FAILED(hr) || s->borderlessOffscreenRtv == nullptr)
    {
        WindowsRenderer_ReleaseBorderlessOffscreen(s);
        return false;
    }
    wchar_t line[192] = {};
    swprintf_s(line, L"[T34][RT] offscreen create %ux%u\r\n", w, h);
    OutputDebugStringW(line);
    return true;
}

static void WindowsRenderer_InternalBindMainRtAndViewport(WindowsRendererState* state);

static bool WindowsRenderer_TryBorderlessOffscreenPresent(WindowsRendererState* state)
{
    const UINT cw = state->clientWidth;
    const UINT ch = state->clientHeight;
    const UINT ow = state->borderlessOffscreenPhysW;
    const UINT oh = state->borderlessOffscreenPhysH;
    if (ow < 1u || oh < 1u || cw < 1u || ch < 1u)
    {
        return false;
    }
    // 安全上限（実験用）。超える場合はフォールバック。
    if (ow > 8192u || oh > 8192u)
    {
        OutputDebugStringW(L"[T34][RT] offscreen skip: dimension > 8192\r\n");
        return false;
    }

    if (!WindowsRenderer_InternalEnsureBorderlessOffscreenRT(state, ow, oh))
    {
        return false;
    }

    const float clearColor[4] = { 0.12f, 0.14f, 0.18f, 1.0f };
    ID3D11RenderTargetView* offRtv = state->borderlessOffscreenRtv;
    state->context->OMSetRenderTargets(1, &offRtv, nullptr);
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(ow);
    vp.Height = static_cast<float>(oh);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    state->context->RSSetViewports(1, &vp);
    state->context->ClearRenderTargetView(offRtv, clearColor);
    state->context->Flush();

    IDXGISurface* osurf = nullptr;
    HRESULT hr = state->borderlessOffscreenTexture->QueryInterface(
        __uuidof(IDXGISurface), reinterpret_cast<void**>(&osurf));
    if (FAILED(hr) || osurf == nullptr)
    {
        WindowsRenderer_InternalBindMainRtAndViewport(state);
        return false;
    }
    {
        wchar_t line[192] = {};
        swprintf_s(line, L"[T34][RT] draw offscreen %ux%u\r\n", ow, oh);
        OutputDebugStringW(line);
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

    (void)WindowsRenderer_DrawD2DContentToSurface(state, osurf, ow, oh, firstDiag);
    if (state->d2dContext != nullptr)
    {
        state->d2dContext->Flush();
    }

    IDXGISurface* osurf2 = nullptr;
    hr = state->borderlessOffscreenTexture->QueryInterface(
        __uuidof(IDXGISurface), reinterpret_cast<void**>(&osurf2));
    if (FAILED(hr) || osurf2 == nullptr)
    {
        WindowsRenderer_InternalBindMainRtAndViewport(state);
        return false;
    }
    UINT dpiSys = GetDpiForWindow(state->targetHwnd);
    if (dpiSys == 0)
    {
        dpiSys = 96u;
    }
    const FLOAT dpiX = static_cast<FLOAT>(dpiSys);
    const FLOAT dpiY = static_cast<FLOAT>(dpiSys);
    const D2D1_BITMAP_PROPERTIES1 srcProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiX,
        dpiY);
    ID2D1Bitmap1* srcBmp = nullptr;
    hr = state->d2dContext->CreateBitmapFromDxgiSurface(osurf2, &srcProps, &srcBmp);
    osurf2->Release();
    if (FAILED(hr) || srcBmp == nullptr)
    {
        wchar_t line[192] = {};
        swprintf_s(
            line,
            L"[T34][RT] composite source bitmap FAILED hr=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        srcBmp = nullptr;
        WindowsRenderer_InternalBindMainRtAndViewport(state);
        return false;
    }

    ID3D11RenderTargetView* rtv = state->rtv;
    state->context->OMSetRenderTargets(1, &rtv, nullptr);
    vp.Width = static_cast<float>(cw);
    vp.Height = static_cast<float>(ch);
    state->context->RSSetViewports(1, &vp);
    state->context->ClearRenderTargetView(rtv, clearColor);
    state->context->Flush();

    {
        wchar_t line[224] = {};
        swprintf_s(line, L"[T34][RT] composite to backbuffer client=%ux%u\r\n", cw, ch);
        OutputDebugStringW(line);
    }

    ID3D11Texture2D* backBuffer = nullptr;
    hr = state->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || backBuffer == nullptr)
    {
        srcBmp->Release();
        return false;
    }
    IDXGISurface* bSurf = nullptr;
    hr = backBuffer->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&bSurf));
    backBuffer->Release();
    if (FAILED(hr) || bSurf == nullptr)
    {
        srcBmp->Release();
        return false;
    }
    const D2D1_BITMAP_PROPERTIES1 dstProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiX,
        dpiY);
    ID2D1Bitmap1* dstBmp = nullptr;
    hr = state->d2dContext->CreateBitmapFromDxgiSurface(bSurf, &dstProps, &dstBmp);
    bSurf->Release();
    if (FAILED(hr) || dstBmp == nullptr)
    {
        srcBmp->Release();
        return false;
    }

    const float sx = 96.f / dpiX;
    const float sy = 96.f / dpiY;
    const float wDipClient = static_cast<float>(cw) * sx;
    const float hDipClient = static_cast<float>(ch) * sy;

    state->d2dContext->SetTarget(dstBmp);
    state->d2dContext->BeginDraw();
    state->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    state->d2dContext->DrawBitmap(
        srcBmp,
        D2D1::RectF(0.f, 0.f, wDipClient, hDipClient),
        1.0f,
        D2D1_INTERPOLATION_MODE_LINEAR,
        nullptr,
        nullptr);
    hr = state->d2dContext->EndDraw();
    dstBmp->Release();
    srcBmp->Release();
    state->d2dContext->SetTarget(nullptr);

    if (FAILED(hr))
    {
        wchar_t line[160] = {};
        swprintf_s(line, L"[T34][RT] composite EndDraw hr=0x%08X\r\n", static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
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
    return true;
}

static bool WindowsRenderer_InternalEnsureFullscreenOffscreenRT(WindowsRendererState* s, UINT w, UINT h)
{
    if (!s || !s->device || w < 1u || h < 1u)
    {
        return false;
    }
    if (s->fullscreenOffscreenTexture != nullptr)
    {
        D3D11_TEXTURE2D_DESC td = {};
        s->fullscreenOffscreenTexture->GetDesc(&td);
        if (td.Width == w && td.Height == h)
        {
            return true;
        }
    }
    WindowsRenderer_ReleaseFullscreenOffscreen(s);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = 0;
    td.MiscFlags = 0;

    HRESULT hr = s->device->CreateTexture2D(&td, nullptr, &s->fullscreenOffscreenTexture);
    if (FAILED(hr) || s->fullscreenOffscreenTexture == nullptr)
    {
        wchar_t line[192] = {};
        swprintf_s(
            line,
            L"[T36][RT] offscreen create FAILED hr=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        return false;
    }
    hr = s->device->CreateRenderTargetView(s->fullscreenOffscreenTexture, nullptr, &s->fullscreenOffscreenRtv);
    if (FAILED(hr) || s->fullscreenOffscreenRtv == nullptr)
    {
        WindowsRenderer_ReleaseFullscreenOffscreen(s);
        OutputDebugStringW(L"[T36][RT] offscreen create FAILED (CreateRenderTargetView)\r\n");
        return false;
    }
    wchar_t line[192] = {};
    swprintf_s(line, L"[T36][RT] offscreen create %ux%u\r\n", w, h);
    OutputDebugStringW(line);
    return true;
}

static bool WindowsRenderer_TryFullscreenOffscreenPresent(WindowsRendererState* state)
{
    const UINT cw = state->clientWidth;
    const UINT ch = state->clientHeight;
    const UINT ow = state->fullscreenOffscreenPhysW;
    const UINT oh = state->fullscreenOffscreenPhysH;
    if (ow < 1u || oh < 1u || cw < 1u || ch < 1u)
    {
        return false;
    }
    if (ow > 8192u || oh > 8192u)
    {
        OutputDebugStringW(L"[T36][RT] skip: dimension > 8192\r\n");
        return false;
    }

    if (!WindowsRenderer_InternalEnsureFullscreenOffscreenRT(state, ow, oh))
    {
        return false;
    }

    const float clearColor[4] = { 0.12f, 0.14f, 0.18f, 1.0f };
    ID3D11RenderTargetView* offRtv = state->fullscreenOffscreenRtv;
    state->context->OMSetRenderTargets(1, &offRtv, nullptr);
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(ow);
    vp.Height = static_cast<float>(oh);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    state->context->RSSetViewports(1, &vp);
    state->context->ClearRenderTargetView(offRtv, clearColor);
    state->context->Flush();

    IDXGISurface* osurf = nullptr;
    HRESULT hr = state->fullscreenOffscreenTexture->QueryInterface(
        __uuidof(IDXGISurface), reinterpret_cast<void**>(&osurf));
    if (FAILED(hr) || osurf == nullptr)
    {
        OutputDebugStringW(L"[T36][RT] fallback: QueryInterface(IDXGISurface) offscreen\r\n");
        WindowsRenderer_InternalBindMainRtAndViewport(state);
        return false;
    }
    {
        wchar_t line[192] = {};
        swprintf_s(line, L"[T36][RT] draw offscreen %ux%u\r\n", ow, oh);
        OutputDebugStringW(line);
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

    (void)WindowsRenderer_DrawD2DContentToSurface(state, osurf, ow, oh, firstDiag);
    osurf->Release();
    osurf = nullptr;
    if (state->d2dContext != nullptr)
    {
        state->d2dContext->Flush();
    }

    IDXGISurface* osurf2 = nullptr;
    hr = state->fullscreenOffscreenTexture->QueryInterface(
        __uuidof(IDXGISurface), reinterpret_cast<void**>(&osurf2));
    if (FAILED(hr) || osurf2 == nullptr)
    {
        OutputDebugStringW(L"[T36][RT] fallback: QueryInterface(IDXGISurface) for composite\r\n");
        WindowsRenderer_InternalBindMainRtAndViewport(state);
        return false;
    }
    UINT dpiSys = GetDpiForWindow(state->targetHwnd);
    if (dpiSys == 0)
    {
        dpiSys = 96u;
    }
    const FLOAT dpiX = static_cast<FLOAT>(dpiSys);
    const FLOAT dpiY = static_cast<FLOAT>(dpiSys);
    const D2D1_BITMAP_PROPERTIES1 srcProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiX,
        dpiY);
    ID2D1Bitmap1* srcBmp = nullptr;
    hr = state->d2dContext->CreateBitmapFromDxgiSurface(osurf2, &srcProps, &srcBmp);
    osurf2->Release();
    if (FAILED(hr) || srcBmp == nullptr)
    {
        wchar_t line[192] = {};
        swprintf_s(
            line,
            L"[T36][RT] composite source bitmap FAILED hr=0x%08X\r\n",
            static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        WindowsRenderer_InternalBindMainRtAndViewport(state);
        return false;
    }

    ID3D11RenderTargetView* rtv = state->rtv;
    state->context->OMSetRenderTargets(1, &rtv, nullptr);
    vp.Width = static_cast<float>(cw);
    vp.Height = static_cast<float>(ch);
    state->context->RSSetViewports(1, &vp);
    state->context->ClearRenderTargetView(rtv, clearColor);
    state->context->Flush();

    {
        wchar_t line[224] = {};
        swprintf_s(line, L"[T36][RT] composite to backbuffer client=%ux%u\r\n", cw, ch);
        OutputDebugStringW(line);
    }

    ID3D11Texture2D* backBuffer = nullptr;
    hr = state->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || backBuffer == nullptr)
    {
        srcBmp->Release();
        OutputDebugStringW(L"[T36][RT] fallback: GetBuffer backbuffer\r\n");
        return false;
    }
    IDXGISurface* bSurf = nullptr;
    hr = backBuffer->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&bSurf));
    backBuffer->Release();
    if (FAILED(hr) || bSurf == nullptr)
    {
        srcBmp->Release();
        OutputDebugStringW(L"[T36][RT] fallback: backbuffer IDXGISurface\r\n");
        return false;
    }
    const D2D1_BITMAP_PROPERTIES1 dstProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiX,
        dpiY);
    ID2D1Bitmap1* dstBmp = nullptr;
    hr = state->d2dContext->CreateBitmapFromDxgiSurface(bSurf, &dstProps, &dstBmp);
    bSurf->Release();
    if (FAILED(hr) || dstBmp == nullptr)
    {
        srcBmp->Release();
        OutputDebugStringW(L"[T36][RT] fallback: CreateBitmapFromDxgiSurface dst\r\n");
        return false;
    }

    const float sx = 96.f / dpiX;
    const float sy = 96.f / dpiY;
    const float wDipClient = static_cast<float>(cw) * sx;
    const float hDipClient = static_cast<float>(ch) * sy;

    state->d2dContext->SetTarget(dstBmp);
    state->d2dContext->BeginDraw();
    state->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    state->d2dContext->DrawBitmap(
        srcBmp,
        D2D1::RectF(0.f, 0.f, wDipClient, hDipClient),
        1.0f,
        D2D1_INTERPOLATION_MODE_LINEAR,
        nullptr,
        nullptr);
    hr = state->d2dContext->EndDraw();
    dstBmp->Release();
    srcBmp->Release();
    state->d2dContext->SetTarget(nullptr);

    if (FAILED(hr))
    {
        wchar_t line[160] = {};
        swprintf_s(line, L"[T36][RT] composite EndDraw hr=0x%08X\r\n", static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        OutputDebugStringW(L"[T36][RT] fallback: EndDraw failed\r\n");
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
    return true;
}

static void WindowsRenderer_InternalBindMainRtAndViewport(WindowsRendererState* state)
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
}

// clear（D3D）→ flush → D2D 1 行（任意）→ Present 1 回
static void WindowsRenderer_Internal_ClearD2DPresent(
    WindowsRendererState* state,
    WindowsRendererPresentationMode presentationMode)
{
    // T35: T34 オフスクリーンは Borderless のみ。それ以外は必ず解放し T34 に入らない
    if (presentationMode != WindowsRendererPresentationMode::Borderless)
    {
        WindowsRenderer_ClearBorderlessOffscreen(state);
    }
    else if (!state->borderlessOffscreenComposite && state->borderlessOffscreenTexture != nullptr)
    {
        WindowsRenderer_ReleaseBorderlessOffscreen(state);
    }

    // T36: Fullscreen 以外では実験用オフスクリーンを解放（T34 と別リソース）
    if (presentationMode != WindowsRendererPresentationMode::Fullscreen)
    {
        WindowsRenderer_ClearFullscreenOffscreen(state);
    }
    else if (!state->fullscreenOffscreenComposite && state->fullscreenOffscreenTexture != nullptr)
    {
        WindowsRenderer_ReleaseFullscreenOffscreen(state);
    }

    if (presentationMode == WindowsRendererPresentationMode::Borderless && state->borderlessOffscreenComposite &&
        state->borderlessOffscreenPhysW >= 1u && state->borderlessOffscreenPhysH >= 1u &&
        state->d2dContext != nullptr)
    {
        if (WindowsRenderer_TryBorderlessOffscreenPresent(state))
        {
            return;
        }
    }

    if (presentationMode == WindowsRendererPresentationMode::Fullscreen && state->fullscreenOffscreenComposite &&
        state->fullscreenOffscreenPhysW >= 1u && state->fullscreenOffscreenPhysH >= 1u &&
        state->d2dContext != nullptr)
    {
        if (WindowsRenderer_TryFullscreenOffscreenPresent(state))
        {
            return;
        }
    }

    WindowsRenderer_InternalBindMainRtAndViewport(state);

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
    WindowsRenderer_ReleaseBorderlessOffscreen(state);
    state->borderlessOffscreenComposite = false;
    state->borderlessOffscreenPhysW = 0;
    state->borderlessOffscreenPhysH = 0;
    WindowsRenderer_ReleaseFullscreenOffscreen(state);
    state->fullscreenOffscreenComposite = false;
    state->fullscreenOffscreenPhysW = 0;
    state->fullscreenOffscreenPhysH = 0;
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
    s_loggedGridOnce = false;
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
void WindowsRenderer_Frame(
    WindowsRendererState* state,
    HWND hwnd,
    WindowsRendererPresentationMode presentationMode)
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

    static int s_lastT35PresentationModeLog = -1;
    const int cur = static_cast<int>(presentationMode);
    if (cur != s_lastT35PresentationModeLog)
    {
        switch (presentationMode)
        {
        case WindowsRendererPresentationMode::Borderless:
            OutputDebugStringW(L"[T35] offscreen enabled: Borderless\r\n");
            break;
        case WindowsRendererPresentationMode::Fullscreen:
            OutputDebugStringW(L"[T35] offscreen disabled: Fullscreen\r\n");
            break;
        case WindowsRendererPresentationMode::Windowed:
            OutputDebugStringW(L"[T35] offscreen disabled: Windowed\r\n");
            break;
        }
        s_lastT35PresentationModeLog = cur;
    }

    WindowsRenderer_Internal_ClearD2DPresent(state, presentationMode);
}
