#include "LogicalInputState.h"

// 1 にすると South の press/release/push/hold を 1 行で OutputDebugString（非アイドル時のみ）
#ifndef LOGICAL_INPUT_DEBUG_SOUTH_LINE
#define LOGICAL_INPUT_DEBUG_SOUTH_LINE 0
#endif

#include <algorithm>
#include <stdio.h>
#if LOGICAL_INPUT_DEBUG_SOUTH_LINE
#include <windows.h>
#endif

void LogicalInputState_Reset(LogicalInputState& st)
{
    st = {};
}

void LogicalInput_FillCurrentDownFromSources(
    bool outDown[static_cast<size_t>(LogicalButtonId::Count)],
    const KeyboardActionState& kb,
    const VirtualInputSnapshot& pad)
{
    const size_t n = static_cast<size_t>(LogicalButtonId::Count);
    for (size_t i = 0; i < n; ++i)
    {
        outDown[i] = false;
    }
    for (UINT8 bi = 0; bi < static_cast<UINT8>(GamepadButtonId::Count); ++bi)
    {
        const auto id = static_cast<GamepadButtonId>(bi);
        outDown[bi] = VirtualInput_IsButtonDown(pad, id);
    }

    outDown[static_cast<size_t>(LogicalButtonId::South)] |= kb.enter;
    outDown[static_cast<size_t>(LogicalButtonId::East)] |= kb.backspace;
    outDown[static_cast<size_t>(LogicalButtonId::Start)] |= kb.tab;
    outDown[static_cast<size_t>(LogicalButtonId::DPadUp)] |= kb.up;
    outDown[static_cast<size_t>(LogicalButtonId::DPadDown)] |= kb.down;
    outDown[static_cast<size_t>(LogicalButtonId::DPadLeft)] |= kb.left;
    outDown[static_cast<size_t>(LogicalButtonId::DPadRight)] |= kb.right;
}

void LogicalInputState_Update(
    LogicalInputState& st,
    const bool currentDown[static_cast<size_t>(LogicalButtonId::Count)])
{
    const size_t n = static_cast<size_t>(LogicalButtonId::Count);
    for (size_t i = 0; i < n; ++i)
    {
        const bool down = currentDown[i];
        const bool prevDown = st.prevDown[i];
        const UINT8 prevHold = st.prevHoldFrames[i];

        const bool release = prevDown && !down;
        const bool press = down;
        const bool push = release && (prevHold == 1u);
        UINT8 hold = 0;
        if (down)
        {
            if (prevDown)
            {
                const unsigned next = static_cast<unsigned>(prevHold) + 1u;
                hold = static_cast<UINT8>((std::min)(next, 255u));
            }
            else
            {
                hold = 1;
            }
        }

        LogicalButtonFrameState& f = st.frames[i];
        f.down = down;
        f.press = press;
        f.release = release;
        f.push = push;
        f.holdFrames = hold;

        st.prevDown[i] = down;
        st.prevHoldFrames[i] = hold;
    }

#if LOGICAL_INPUT_DEBUG_SOUTH_LINE
    {
        const LogicalButtonFrameState& sb =
            st.frames[static_cast<size_t>(LogicalButtonId::South)];
        const bool activity =
            sb.down || sb.release || sb.push || sb.holdFrames != 0;
        if (activity)
        {
            wchar_t w[192] = {};
            swprintf_s(
                w,
                _countof(w),
                L"[LOGICAL] South: press=%d release=%d push=%d hold=%u\r\n",
                sb.press ? 1 : 0,
                sb.release ? 1 : 0,
                sb.push ? 1 : 0,
                static_cast<unsigned int>(sb.holdFrames));
            OutputDebugStringW(w);
        }
    }
#endif
}
