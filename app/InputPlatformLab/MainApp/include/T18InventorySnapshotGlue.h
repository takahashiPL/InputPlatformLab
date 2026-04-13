// T18 app/debug glue: snapshot struct, survey->snapshot classify path, rationale, HUD summaries.
// Post-foundation step5: split from MainApp.cpp; not platform/win, not input/core.
// Paged HUD T18 body line layout: T18PageBodyFormatGlue.* (step6).
#pragma once

#include "ControllerClassification.h"
#include "Win32InputGlue.h"

struct T18ControllerIdentifySnapshot
{
    int xinput_slot; // -1: 未接続
    bool hid_found;
    GameControllerHidSummary hid;
    wchar_t device_path[512];
    wchar_t product_name[256];
    GameControllerKind inferred_kind;
    ControllerParserKind parser_kind;
    ControllerSupportLevel support_level;
    wchar_t rationale[512]; // 表示用: family/parser/support が tentative な理由（断定しない）
};

void T18Inventory_CompleteSnapshotFromSurvey(
    T18ControllerIdentifySnapshot& snap,
    const Win32InputGlue_T18InventorySurvey& inv);

void T18Inventory_FillIdentifyRationale(const T18ControllerIdentifySnapshot& s, wchar_t* buf, size_t bufCount);
void T18Inventory_OutputSnapshotDebugLines(const T18ControllerIdentifySnapshot& snap);
void T18Inventory_FillWhyHudShort(const T18ControllerIdentifySnapshot& s, wchar_t* buf, size_t bufCount);
void T18Inventory_FillWhyHudSingleLine(const T18ControllerIdentifySnapshot& s, wchar_t* buf, size_t bufCount);
void T18Inventory_TruncateWideForPaint(const wchar_t* src, wchar_t* dst, size_t dstCount, size_t maxLen);
