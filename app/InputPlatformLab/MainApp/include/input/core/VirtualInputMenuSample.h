// T26: VirtualInputConsumerFrame + メニュー試作（Win32 / XInput 非依存）。header-only。
#pragma once

#include "CommonTypes.h"

// T21: 消費側 1 フレーム分（VirtualInputSnapshot / XInput を知らない層向け）
struct VirtualInputConsumerFrame
{
    INT8 moveX;
    INT8 moveY;
    bool confirmPressed;
    bool cancelPressed;
    bool menuPressed;
};

// T22/T23: 2x2 サンプルメニュー状態機械（入力は VirtualInputConsumerFrame のみ）
//
// 固定仕様（ここ以外にロジックを書かない）:
// - menuPressed        -> menuOpen を toggle
// - confirmPressed     -> menuOpen のとき activate イベント（選択は変えない）
// - cancelPressed      -> cancel イベント。menuOpen なら false にし menuClosedByCancel
// - moveX/moveY        -> prev が 0 かつ今フレームが非 0 のときだけ 1 マス（repeat なし）
// - selectionX/Y       -> 0..1 にクランプ。menuOpen=false のときは移動しない
// - 毎フレーム末尾で prevMoveX/Y <- f.moveX/moveY（次フレームのエッジ検出用）
struct VirtualInputMenuSampleState
{
    bool menuOpen;
    INT8 selectionX;
    INT8 selectionY;
    INT8 prevMoveX;
    INT8 prevMoveY;
};

struct VirtualInputMenuSampleEvents
{
    bool menuToggled;
    bool selectionChanged;
    bool activated;
    bool cancelled;
    bool menuClosedByCancel;
};

inline void VirtualInputMenuSample_Reset(VirtualInputMenuSampleState& s)
{
    s = {};
}

inline INT8 VirtualInputMenuSample_ClampSelection(INT8 v)
{
    if (v < 0)
    {
        return 0;
    }
    if (v > 1)
    {
        return 1;
    }
    return v;
}

inline VirtualInputMenuSampleEvents VirtualInputMenuSample_Apply(
    VirtualInputMenuSampleState& s,
    const VirtualInputConsumerFrame& f)
{
    VirtualInputMenuSampleEvents ev{};

    if (f.menuPressed)
    {
        s.menuOpen = !s.menuOpen;
        ev.menuToggled = true;
    }

    if (f.confirmPressed && s.menuOpen)
    {
        ev.activated = true;
    }

    if (f.cancelPressed)
    {
        ev.cancelled = true;
        if (s.menuOpen)
        {
            s.menuOpen = false;
            ev.menuClosedByCancel = true;
        }
    }

    if (s.menuOpen)
    {
        const INT8 osx = s.selectionX;
        const INT8 osy = s.selectionY;
        const bool mxEdge = (s.prevMoveX == 0 && f.moveX != 0);
        const bool myEdge = (s.prevMoveY == 0 && f.moveY != 0);
        if (mxEdge)
        {
            s.selectionX = VirtualInputMenuSample_ClampSelection(
                static_cast<INT8>(s.selectionX + f.moveX));
        }
        if (myEdge)
        {
            s.selectionY = VirtualInputMenuSample_ClampSelection(
                static_cast<INT8>(s.selectionY + f.moveY));
        }
        if (osx != s.selectionX || osy != s.selectionY)
        {
            ev.selectionChanged = true;
        }
    }

    s.prevMoveX = f.moveX;
    s.prevMoveY = f.moveY;
    return ev;
}
