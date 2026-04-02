// T23: Windows 描画・将来 DirectX レンダラ受け皿
//
// 将来ここへ集約する責務（現状は MainApp.cpp 内）:
//   - WM_PAINT、デバッグオーバーレイ（GDI TextOut / DrawText）、スクロール（WM_VSCROLL / WM_MOUSEWHEEL）
//   - 将来: DXGI / D3D11+ 初期化、スワップチェーン、フレーム描画（T23 では未実装）
//
// 本ファイルはヘッダのみ。DirectX SDK 依存の include は後続タスクで追加する。
#pragma once
