// T23: Windows アプリシェル（エントリ・ウィンドウ・メッセージループ）
//
// 将来ここへ集約する責務（現状は MainApp.cpp 内）:
//   - wWinMain、メッセージループ（GetMessage / DispatchMessage）
//   - MyRegisterClass、InitInstance、リソース（アクセラレータ等）
//   - WndProc および WM_COMMAND / WM_DESTROY / WM_INITDIALOG（About）などアプリ枠の分岐
//
// InputCore.h とは独立（Win32 型を中立層へ出さない）。
#pragma once
