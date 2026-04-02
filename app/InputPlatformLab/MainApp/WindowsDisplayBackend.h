// T23: Windows 表示・解像度バックエンド
//
// 将来ここへ集約する責務（現状は MainApp.cpp 内）:
//   - モニタ列挙・表示モードキャッシュ（EnumDisplayMonitors / EnumDisplaySettingsW）
//   - T14 一覧選択、T15 希望解像度の最近傍、T17 プレゼンテーション（Windowed / Borderless / Fullscreen）
//   - T16 ウィンドウ再作成・DPI・ワークエリアクランプ、CreateWindow パラメータ
//
// DirectX スワップチェーンは入れない（T23 時点）。レンダラは WindowsRenderer.h を参照。
#pragma once
