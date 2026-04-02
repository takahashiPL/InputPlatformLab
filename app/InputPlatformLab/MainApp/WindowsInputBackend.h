// T23: Windows 入力バックエンド（Raw Input / XInput / キー）
//
// 将来ここへ集約する責務（現状は MainApp.cpp 内）:
//   - RegisterRawInputDevices、WM_INPUT（HID / キー）、PhysicalKeyEvent への橋渡し
//   - XInput ポーリング（タイマー TIMER_ID_XINPUT_POLL）、スロット列挙・VirtualInputSnapshot 生成
//   - DS4 既知 HID レポート解釈、汎用 HID フォールバックログ、コントローラ列挙ログ
//
// 中立型・分類は InputCore.h（ControllerClassification / VirtualInputNeutral）。本ヘッダは Win32 専用実装の受け皿。
#pragma once
