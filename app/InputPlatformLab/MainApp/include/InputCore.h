// T22: 他プロジェクト向け中立入力・分類の単一入口（Win32 / Raw Input / XInput / HWND を含まない）
// Pack-out: reusable portable foundation umbrella — link VirtualInputNeutral / LogicalInputState / ControllerClassification .cpp.
//
// 公開面（型・宣言）:
//   - GameControllerKind, GamepadButtonId, GamepadLeftStickDir — GamepadTypes.h
//   - VirtualInputSnapshot, VirtualInputPolicyHeld, VirtualInputPolicyMenuEdges,
//     KeyboardActionState — VirtualInputNeutral.h
//   - VirtualInputConsumerFrame — VirtualInputMenuSample.h
//   - LogicalButtonId, LogicalInputState, InputCore_LogicalInputState — LogicalInputState.h
//   - ControllerParserKind, ControllerSupportLevel, GameControllerHidSummary — ControllerClassification.h
//
// 実装は VirtualInputNeutral.cpp / LogicalInputState.cpp / ControllerClassification.cpp をリンクすること。
#pragma once

#include "CommonTypes.h"
#include "GamepadTypes.h"
#include "VirtualInputMenuSample.h"
#include "VirtualInputNeutral.h"
#include "LogicalInputState.h"
#include "ControllerClassification.h"
