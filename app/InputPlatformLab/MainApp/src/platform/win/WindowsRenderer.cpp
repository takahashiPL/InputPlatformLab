#include "WindowsRenderer.h"

#include <algorithm>
#include <cmath>
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
    (void)s->dwriteTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

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

// T37 有効時の左上グリッド短文化用（1 行帯）
static const wchar_t* WindowsRenderer_GridBasisAbbrev(WindowsRendererGridDebugBasis b)
{
    switch (b)
    {
    case WindowsRendererGridDebugBasis::CommittedSelected:
        return L"cmtSel";
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
    // T37 本文と T33 が重なりにくいよう、committed/client を 1 行で画面上部右寄せ帯に退避。
    if (s->t37VirtualBodyOverlayRequested)
    {
        wchar_t cap[512] = {};
        if (s->gridDebugCommittedPhysW > 0u && s->gridDebugCommittedPhysH > 0u)
        {
            swprintf_s(
                cap,
                _countof(cap),
                L"b:%s  cmt=%ux%u  cl=%ux%u  cell=%u",
                WindowsRenderer_GridBasisAbbrev(s->gridDebugBasis),
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
                L"b:%s  cmt=n/a  cl=%ux%u  cell=%u",
                WindowsRenderer_GridBasisAbbrev(s->gridDebugBasis),
                static_cast<unsigned int>(s->gridDebugClientPhysW),
                static_cast<unsigned int>(s->gridDebugClientPhysH),
                static_cast<unsigned int>(stepPx));
        }
        const float splitX = (std::max)(80.f, wDip * 0.5f);
        const D2D1_RECT_F layoutTop = D2D1::RectF(splitX, 4.f, wDip - 4.f, 28.f);
        s->d2dContext->DrawText(
            cap,
            static_cast<UINT32>(wcslen(cap)),
            s->dwriteGridLabelFormat,
            layoutTop,
            s->d2dGridBrushEm,
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
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
    // cand/act・左列メニューは committed に乗せず、最終 backbuffer 専用の DrawHudD2DOnFinalBackbuffer で描画。

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

// 最終 swapchain backbuffer: T39 3 帯（row1 cand/act, row2 menu, body T14 スクロール）+ 下端 [scroll]
static HRESULT WindowsRenderer_DrawHudD2DOnFinalBackbuffer(
    WindowsRendererState* s,
    UINT cw,
    UINT ch,
    bool firstDiag)
{
    if (!s->d2dContext || !s->dwriteTextFormat || !s->d2dTextBrush || !s->swapChain || cw < 1u || ch < 1u)
    {
        return E_INVALIDARG;
    }
    s->dbgHudLeftColumnSkipGdi = false;
    s->dbgHudScrollBandSkipGdi = false;

    UINT dpiSys = GetDpiForWindow(s->targetHwnd);
    if (dpiSys == 0)
    {
        dpiSys = 96u;
    }
    const FLOAT dpiX = static_cast<FLOAT>(dpiSys);
    const FLOAT dpiY = static_cast<FLOAT>(dpiSys);
    const float sx = 96.f / dpiX;
    const float sy = 96.f / dpiY;
    const float wDip = static_cast<float>(cw) * sx;
    const float hDip = static_cast<float>(ch) * sy;

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = s->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (FAILED(hr) || backBuffer == nullptr)
    {
        return hr;
    }
    IDXGISurface* bSurf = nullptr;
    hr = backBuffer->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&bSurf));
    backBuffer->Release();
    if (FAILED(hr) || bSurf == nullptr)
    {
        return hr;
    }
    const D2D1_BITMAP_PROPERTIES1 dstProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpiX,
        dpiY);
    ID2D1Bitmap1* dstBmp = nullptr;
    hr = s->d2dContext->CreateBitmapFromDxgiSurface(bSurf, &dstProps, &dstBmp);
    bSurf->Release();
    if (FAILED(hr) || dstBmp == nullptr)
    {
        return hr;
    }

    static const wchar_t kFallbackT33Line[] = L"T33: DirectWrite overlay (1 line)";
    const wchar_t* const row1CandPtr =
        (s->t17HudLine[0] != L'\0') ? s->t17HudLine : kFallbackT33Line;
    const UINT32 row1CandLen = static_cast<UINT32>(wcslen(row1CandPtr));

    s->d2dContext->SetTarget(dstBmp);
    s->d2dContext->BeginDraw();
    s->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());

    static constexpr float kFinalHudMarginDip = 8.0f;
    bool drewLeftHudToD2d = false;
    const bool prefillOk =
        cw == s->dbgHudLeftColumnPrefillClientW && ch == s->dbgHudLeftColumnPrefillClientH &&
        s->dbgHudLeftColumnClipBottomPadPx >= 0;
    if (prefillOk && s->dbgHudBodyBandText[0] != L'\0' && s->dbgHudMenuBandText[0] != L'\0' &&
        s->dbgHudFinalBodyTopPx > 0 && s->dbgHudBodyT14DocTopPx >= 0 && s->dbgHudRow2TopPx >= 0)
    {
        const float bottomPadDip =
            static_cast<float>(s->dbgHudLeftColumnClipBottomPadPx) * (96.f / dpiY);
        const float clipBottomDip = (std::max)(0.f, hDip - bottomPadDip);
        const float bodyTopDip = static_cast<float>(s->dbgHudFinalBodyTopPx) * sy;

        const D2D1_RECT_F clipBody =
            D2D1::RectF(0.f, bodyTopDip, wDip, clipBottomDip);
        s->d2dContext->PushAxisAlignedClip(clipBody, D2D1_ANTIALIAS_MODE_ALIASED);

        if (s->dbgHudT14VmSplit && s->dbgHudT14PrefixText[0] != L'\0' && s->dbgHudT14RestText[0] != L'\0')
        {
            const float t14TextStartDip =
                static_cast<float>(s->dbgHudRow2TopPx + s->dbgHudBodyT14DocTopPx) * sy;
            const float prefixHDip = static_cast<float>(s->dbgHudT14PrefixHeightPx) * sy;
            const float vmBandHDip = static_cast<float>(s->dbgHudVmBandHeightPx) * sy;
            const float restScrollDip = static_cast<float>(s->dbgHudLeftColumnScrollYPx) * sy;
            const float restStartDip = t14TextStartDip + prefixHDip + vmBandHDip;

            const UINT32 prefixLen =
                static_cast<UINT32>((std::min)(wcslen(s->dbgHudT14PrefixText), size_t{8191}));
            const D2D1_RECT_F rcPref = D2D1::RectF(
                0.f,
                t14TextStartDip,
                wDip,
                t14TextStartDip + prefixHDip + 8.f);
            s->d2dContext->DrawText(
                s->dbgHudT14PrefixText,
                prefixLen,
                s->dwriteTextFormat,
                rcPref,
                s->d2dTextBrush,
                D2D1_DRAW_TEXT_OPTIONS_NONE,
                DWRITE_MEASURING_MODE_NATURAL);

            wchar_t vmBandBuf[8192] = {};
            wcscpy_s(vmBandBuf, _countof(vmBandBuf), s->dbgHudVmHeadingText);
            wcscat_s(vmBandBuf, _countof(vmBandBuf), s->dbgHudVmListBandText);
            const UINT32 vmLen =
                static_cast<UINT32>((std::min)(wcslen(vmBandBuf), size_t{8191}));
            const D2D1_RECT_F rcVm = D2D1::RectF(
                0.f,
                t14TextStartDip + prefixHDip,
                wDip,
                restStartDip + 8.f);
            s->d2dContext->DrawText(
                vmBandBuf,
                vmLen,
                s->dwriteTextFormat,
                rcVm,
                s->d2dTextBrush,
                D2D1_DRAW_TEXT_OPTIONS_NONE,
                DWRITE_MEASURING_MODE_NATURAL);

            const D2D1_RECT_F clipRest =
                D2D1::RectF(0.f, restStartDip, wDip, clipBottomDip);
            s->d2dContext->PushAxisAlignedClip(clipRest, D2D1_ANTIALIAS_MODE_ALIASED);
            s->d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(0.f, -restScrollDip));
            const UINT32 restLen =
                static_cast<UINT32>((std::min)(wcslen(s->dbgHudBodyBandText), size_t{16383}));
            const D2D1_RECT_F docRest = D2D1::RectF(
                0.f,
                restStartDip,
                wDip,
                restStartDip + 2000000.f);
            s->d2dContext->DrawText(
                s->dbgHudBodyBandText,
                restLen,
                s->dwriteTextFormat,
                docRest,
                s->d2dTextBrush,
                D2D1_DRAW_TEXT_OPTIONS_NONE,
                DWRITE_MEASURING_MODE_NATURAL);
            s->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
            s->d2dContext->PopAxisAlignedClip();
        }
        else
        {
            const int scrollBodyPx = (std::max)(
                0,
                s->dbgHudLeftColumnScrollYPx + s->dbgHudFinalBodyTopPx - s->dbgHudBodyT14DocTopPx);
            const float scrollBodyDip = static_cast<float>(scrollBodyPx) * sy;
            s->d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(0.f, -scrollBodyDip));
            const UINT32 bodyLen =
                static_cast<UINT32>((std::min)(wcslen(s->dbgHudBodyBandText), size_t{16383}));
            const D2D1_RECT_F docBody = D2D1::RectF(
                0.f,
                bodyTopDip,
                wDip,
                bodyTopDip + 2000000.f);
            s->d2dContext->DrawText(
                s->dbgHudBodyBandText,
                bodyLen,
                s->dwriteTextFormat,
                docBody,
                s->d2dTextBrush,
                D2D1_DRAW_TEXT_OPTIONS_NONE,
                DWRITE_MEASURING_MODE_NATURAL);
            s->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        }
        s->d2dContext->PopAxisAlignedClip();

        const float row1HDip = static_cast<float>(s->dbgHudFinalRow1HeightPx) * sy;
        const float row2TopDip = static_cast<float>(s->dbgHudRow2TopPx) * sy;
        const float menuHDip = static_cast<float>(s->dbgHudMenuColumnHeightPx) * sy;
        const float row2BottomDip = (std::min)(bodyTopDip - 1.f, row2TopDip + menuHDip);

        const D2D1_RECT_F row1Rect = D2D1::RectF(
            kFinalHudMarginDip,
            0.f,
            (std::max)(kFinalHudMarginDip + 4.f, wDip - kFinalHudMarginDip),
            row1HDip + 4.f);
        s->d2dContext->DrawText(
            row1CandPtr,
            row1CandLen,
            s->dwriteTextFormat,
            row1Rect,
            s->d2dTextBrush,
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
        if (firstDiag)
        {
            T33_LogFrameStep(L"[HUD] DrawText row1 cand/act (T39)", S_OK);
        }

        const UINT32 menuLen =
            static_cast<UINT32>((std::min)(wcslen(s->dbgHudMenuBandText), size_t{3071}));
        const D2D1_RECT_F row2Rect = D2D1::RectF(
            kFinalHudMarginDip,
            row2TopDip,
            (std::max)(kFinalHudMarginDip + 4.f, wDip - kFinalHudMarginDip),
            (std::max)(row2TopDip + 4.f, row2BottomDip));
        s->d2dContext->DrawText(
            s->dbgHudMenuBandText,
            menuLen,
            s->dwriteTextFormat,
            row2Rect,
            s->d2dTextBrush,
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
        if (firstDiag)
        {
            T33_LogFrameStep(L"[HUD] DrawText row2 menu (T39)", S_OK);
        }
        if (firstDiag)
        {
            T33_LogFrameStep(L"[HUD] DrawText body T14 (T39)", S_OK);
        }
        drewLeftHudToD2d = true;
    }
    else if (s->dbgHudLeftColumnText[0] != L'\0' && prefillOk)
    {
        const float scrollDip =
            static_cast<float>(s->dbgHudLeftColumnScrollYPx) * (96.f / dpiY);
        const float menuTopDip = static_cast<float>(s->dbgHudLeftColumnTopPx) * (96.f / dpiY);
        const float bottomPadDip =
            static_cast<float>(s->dbgHudLeftColumnClipBottomPadPx) * (96.f / dpiY);
        const float clipBottomDip = (std::max)(0.f, hDip - bottomPadDip);
        const D2D1_RECT_F clipHud = D2D1::RectF(0.f, 0.f, wDip, clipBottomDip);
        s->d2dContext->PushAxisAlignedClip(clipHud, D2D1_ANTIALIAS_MODE_ALIASED);
        s->d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(0.f, -scrollDip));
        const UINT32 hudLen =
            static_cast<UINT32>((std::min)(wcslen(s->dbgHudLeftColumnText), size_t{16383}));
        const D2D1_RECT_F docRect = D2D1::RectF(
            0.f,
            menuTopDip,
            wDip,
            menuTopDip + 2000000.f);
        s->d2dContext->DrawText(
            s->dbgHudLeftColumnText,
            hudLen,
            s->dwriteTextFormat,
            docRect,
            s->d2dTextBrush,
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
        s->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        s->d2dContext->PopAxisAlignedClip();
        drewLeftHudToD2d = true;
        if (firstDiag)
        {
            T33_LogFrameStep(L"[HUD] DrawText left column (menu+t14) fallback", S_OK);
        }
        const float t33Top = 8.0f;
        const D2D1_RECT_F layout = D2D1::RectF(
            8.0f,
            t33Top,
            (std::max)(8.0f, wDip - 8.0f),
            (std::max)(t33Top + 4.0f, hDip - 8.0f));
        s->d2dContext->DrawText(
            row1CandPtr,
            row1CandLen,
            s->dwriteTextFormat,
            layout,
            s->d2dTextBrush,
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
        if (firstDiag)
        {
            T33_LogFrameStep(L"[HUD] DrawText cand/act fallback", S_OK);
        }
    }
    else
    {
        const float t33Top = 8.0f;
        const D2D1_RECT_F layout = D2D1::RectF(
            8.0f,
            t33Top,
            (std::max)(8.0f, wDip - 8.0f),
            (std::max)(t33Top + 4.0f, hDip - 8.0f));
        s->d2dContext->DrawText(
            row1CandPtr,
            row1CandLen,
            s->dwriteTextFormat,
            layout,
            s->d2dTextBrush,
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
        if (firstDiag)
        {
            T33_LogFrameStep(L"[HUD] DrawText cand/act only", S_OK);
        }
    }

    bool drewScrollBandToD2d = false;
    if (s->dbgHudScrollBandText[0] != L'\0' && s->dbgHudScrollBandHeightPx > 0 &&
        cw == s->dbgHudLeftColumnPrefillClientW && ch == s->dbgHudLeftColumnPrefillClientH)
    {
        const float bandHDip = static_cast<float>(s->dbgHudScrollBandHeightPx) * sy;
        const float topBand = (std::max)(0.f, hDip - bandHDip);
        const D2D1_RECT_F scrollRect = D2D1::RectF(0.f, topBand, wDip, hDip);
        const UINT32 scrollLen = static_cast<UINT32>(
            (std::min)(wcslen(s->dbgHudScrollBandText), size_t{2047}));
        s->d2dContext->DrawText(
            s->dbgHudScrollBandText,
            scrollLen,
            s->dwriteTextFormat,
            scrollRect,
            s->d2dTextBrush,
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
        drewScrollBandToD2d = true;
        if (firstDiag)
        {
            T33_LogFrameStep(L"[HUD] DrawText scroll band (T38)", S_OK);
        }
    }

    hr = s->d2dContext->EndDraw();
    if (SUCCEEDED(hr) && drewLeftHudToD2d)
    {
        s->dbgHudLeftColumnSkipGdi = true;
    }
    if (SUCCEEDED(hr) && drewScrollBandToD2d)
    {
        s->dbgHudScrollBandSkipGdi = true;
    }
    dstBmp->Release();
    s->d2dContext->SetTarget(nullptr);
    if (FAILED(hr) && hr != D2DERR_RECREATE_TARGET && !firstDiag)
    {
        wchar_t line[160] = {};
        swprintf_s(line, L"[HUD] EndDraw hr=0x%08X\r\n", static_cast<unsigned int>(hr));
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
    (void)WindowsRenderer_DrawHudD2DOnFinalBackbuffer(s, s->clientWidth, s->clientHeight, firstDiag);
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

// T37 / T37-1 / T37-2: オフスクリーン合成済みバックバッファ上に本文を DWrite で重ねる。
// committed 幅に応じた段階的スケール + グリッド+T33 下・GDI scroll 帯を避けた本文専用矩形でクリップ。
static bool WindowsRenderer_TryDrawT37VirtualBodyOnCompositeTarget(
    WindowsRendererState* state,
    float wDipClient,
    float hDipClient,
    FLOAT dpiX,
    FLOAT dpiY)
{
    if (!state->t37VirtualBodyOverlayRequested || state->t37BodyText[0] == L'\0')
    {
        return false;
    }
    if (!state->dwriteFactory || !state->d2dContext || !state->dwriteTextFormat || !state->d2dTextBrush)
    {
        OutputDebugStringW(L"[T37] fallback: DWrite/D2D not ready\r\n");
        return false;
    }
    const UINT vw = state->gridDebugCommittedPhysW;
    const UINT vh = state->gridDebugCommittedPhysH;
    if (vw < 1u || vh < 1u)
    {
        OutputDebugStringW(L"[T37] fallback: invalid committed virtual size\r\n");
        return false;
    }
    const float vWdip = static_cast<float>(vw) * (96.f / dpiX);
    const float vHdip = static_cast<float>(vh) * (96.f / dpiY);
    if (vWdip < 1.f || vHdip < 1.f)
    {
        return false;
    }

    static constexpr float kT37BottomReserveDip = 96.f;
    static constexpr float kT37SideMarginDip = 8.f;
#if WIN32_RENDERER_DEBUG_GRID_64PX
    static constexpr float kT37TopReserveDipDefault = 108.f;
#else
    static constexpr float kT37TopReserveDipDefault = 32.f;
#endif
    // 低 committed 幅では上段（グリッド+T33）と干渉しにくいよう上余白を追加（本文開始 Y を下げる）
    const float kT37TopReserveDip =
        (vw <= 800u) ? (kT37TopReserveDipDefault + 20.f) : kT37TopReserveDipDefault;

    const float bodyLeft = kT37SideMarginDip;
    const float bodyTop = kT37TopReserveDip;
    const float bodyW = (std::max)(1.f, wDipClient - 2.f * kT37SideMarginDip);
    const float bodyH = (std::max)(1.f, hDipClient - kT37TopReserveDip - kT37BottomReserveDip);

    const float scaleFit = (std::min)(bodyW / vWdip, bodyH / vHdip);
    // committed 物理幅に応じた段階ルール（640 帯は抑える / 1280 前後は従来に近い / 4K 帯はやや持ち上げ）
    float scalePreMul = 1.f;
    float scaleMinBand = 0.42f;
    float scaleMaxBand = 1.85f;
    if (vw <= 800u)
    {
        scalePreMul = 0.70f;
        scaleMinBand = 0.30f;
        scaleMaxBand = 1.10f;
    }
    else if (vw < 3840u)
    {
        scalePreMul = 1.f;
        scaleMinBand = 0.42f;
        scaleMaxBand = 1.85f;
    }
    else
    {
        scalePreMul = 1.10f;
        scaleMinBand = 0.50f;
        scaleMaxBand = 2.05f;
    }
    float scaleEff = scaleFit * scalePreMul;
    if (scaleEff < scaleMinBand)
    {
        scaleEff = scaleMinBand;
    }
    if (scaleEff > scaleMaxBand)
    {
        scaleEff = scaleMaxBand;
    }

    const float scrollVirtDip = static_cast<float>(state->t37ScrollVirtualPx) * (96.f / dpiY);
    const D2D1_MATRIX_3X2_F xform = D2D1::Matrix3x2F::Translation(bodyLeft, bodyTop) *
        D2D1::Matrix3x2F::Scale(scaleEff, scaleEff) *
        D2D1::Matrix3x2F::Translation(0.f, -scrollVirtDip);

    const size_t rawLen = wcslen(state->t37BodyText);
    const UINT textLen = static_cast<UINT>((std::min)(static_cast<size_t>(8191), rawLen));
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = state->dwriteFactory->CreateTextLayout(
        state->t37BodyText,
        textLen,
        state->dwriteTextFormat,
        vWdip,
        1048576.0f,
        &layout);
    if (FAILED(hr) || layout == nullptr)
    {
        wchar_t line[128] = {};
        swprintf_s(line, L"[T37] fallback: CreateTextLayout hr=0x%08X\r\n", static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
        return false;
    }
    hr = layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (FAILED(hr))
    {
        layout->Release();
        return false;
    }

    state->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    const D2D1_RECT_F clipRect = D2D1::RectF(bodyLeft, bodyTop, bodyLeft + bodyW, bodyTop + bodyH);
    state->d2dContext->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    state->d2dContext->SetTransform(xform);
    state->d2dContext->DrawTextLayout(
        D2D1::Point2F(0.f, 0.f),
        layout,
        state->d2dTextBrush,
        D2D1_DRAW_TEXT_OPTIONS_NONE);
    state->d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    state->d2dContext->PopAxisAlignedClip();
    layout->Release();

    static float s_lastScaleEff = -1.f;
    static float s_lastBodyLeft = 1e10f;
    static float s_lastBodyTop = 1e10f;
    static int s_lastClipW = -1;
    static int s_lastClipH = -1;
    const int clipWi = static_cast<int>(bodyW + 0.5f);
    const int clipHi = static_cast<int>(bodyH + 0.5f);
    if (std::fabs(static_cast<double>(scaleEff - s_lastScaleEff)) > 1e-4 ||
        std::fabs(static_cast<double>(bodyLeft - s_lastBodyLeft)) > 1e-3 ||
        std::fabs(static_cast<double>(bodyTop - s_lastBodyTop)) > 1e-3 || clipWi != s_lastClipW ||
        clipHi != s_lastClipH)
    {
        s_lastScaleEff = scaleEff;
        s_lastBodyLeft = bodyLeft;
        s_lastBodyTop = bodyTop;
        s_lastClipW = clipWi;
        s_lastClipH = clipHi;
        wchar_t line[192] = {};
        swprintf_s(line, _countof(line), L"[T37] layout scale=%.4f\r\n", static_cast<double>(scaleEff));
        OutputDebugStringW(line);
        swprintf_s(
            line,
            _countof(line),
            L"[T37] layout origin=(%.1f,%.1f)\r\n",
            static_cast<double>(bodyLeft),
            static_cast<double>(bodyTop));
        OutputDebugStringW(line);
        swprintf_s(line, _countof(line), L"[T37] layout clip=%dx%d\r\n", clipWi, clipHi);
        OutputDebugStringW(line);
    }
    return true;
}

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
    bool t37Drawn = false;
    if (state->t37VirtualBodyOverlayRequested)
    {
        t37Drawn = WindowsRenderer_TryDrawT37VirtualBodyOnCompositeTarget(
            state, wDipClient, hDipClient, dpiX, dpiY);
    }
    hr = state->d2dContext->EndDraw();
    state->t37VirtualBodyOverlayRenderedOk = t37Drawn && SUCCEEDED(hr);
    dstBmp->Release();
    srcBmp->Release();
    state->d2dContext->SetTarget(nullptr);

    if (FAILED(hr))
    {
        wchar_t line[160] = {};
        swprintf_s(line, L"[T34][RT] composite EndDraw hr=0x%08X\r\n", static_cast<unsigned int>(hr));
        OutputDebugStringW(line);
    }

    (void)WindowsRenderer_DrawHudD2DOnFinalBackbuffer(state, cw, ch, firstDiag);

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
    bool t37DrawnFs = false;
    if (state->t37VirtualBodyOverlayRequested)
    {
        t37DrawnFs = WindowsRenderer_TryDrawT37VirtualBodyOnCompositeTarget(
            state, wDipClient, hDipClient, dpiX, dpiY);
    }
    hr = state->d2dContext->EndDraw();
    state->t37VirtualBodyOverlayRenderedOk = t37DrawnFs && SUCCEEDED(hr);
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

    (void)WindowsRenderer_DrawHudD2DOnFinalBackbuffer(state, cw, ch, firstDiag);

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
    state->t37VirtualBodyOverlayRequested = false;
    state->t37VirtualBodyOverlayRenderedOk = false;
    state->t37ScrollVirtualPx = 0;
    state->t37BodyText[0] = L'\0';
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
