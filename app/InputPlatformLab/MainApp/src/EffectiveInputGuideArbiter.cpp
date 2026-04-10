// T76: separates device inventory (T18) from UI guide family / effective owner (single-player; slot-ready naming).

#include "EffectiveInputGuideArbiter.h"

#include <Windows.h>
#include <cstdio>

namespace
{
#if defined(_DEBUG)
constexpr bool kT76DebugLog = true;
#else
constexpr bool kT76DebugLog = false;
#endif

// ~33ms timer: 6 ticks ~= 200ms (within 150–300ms stabilization window).
constexpr DWORD kOwnerIdealStableMs = 200u;

bool ConsumerFrameHasMeaningfulAction(const VirtualInputConsumerFrame& f)
{
    return f.confirmPressed || f.cancelPressed || f.menuPressed || f.moveX != 0 || f.moveY != 0;
}

InputGuideSourceKind ComputeRawIdealOwner(
    bool kb,
    bool pad,
    InputGuideSourceKind committedOwner)
{
    if (kb && !pad)
    {
        return InputGuideSourceKind::Keyboard;
    }
    if (pad && !kb)
    {
        return InputGuideSourceKind::Gamepad;
    }
    if (kb && pad)
    {
        if (committedOwner == InputGuideSourceKind::Unknown)
        {
            return InputGuideSourceKind::Keyboard;
        }
        return committedOwner;
    }
    return committedOwner;
}

DWORD ElapsedMs(DWORD nowTick, DWORD startTick)
{
    return nowTick - startTick;
}

const wchar_t* SourceKindLabel(InputGuideSourceKind k)
{
    switch (k)
    {
    case InputGuideSourceKind::Keyboard:
        return L"Keyboard";
    case InputGuideSourceKind::Gamepad:
        return L"Gamepad";
    default:
        return L"Unknown";
    }
}

const wchar_t* FamilyShortLabel(GameControllerKind k)
{
    switch (k)
    {
    case GameControllerKind::Xbox:
        return L"Xbox";
    case GameControllerKind::PlayStation4:
        return L"PS4";
    case GameControllerKind::PlayStation5:
        return L"PS5";
    case GameControllerKind::Nintendo:
        return L"Nintendo";
    case GameControllerKind::XInputCompatible:
        return L"XInputCompat";
    default:
        return L"Unknown";
    }
}

struct SinglePlayerGuideArbiter
{
    InputGuideSourceKind ownerSource = InputGuideSourceKind::Unknown;
    GameControllerKind inventoryPadFamily = GameControllerKind::Unknown;
    bool inventoryPadPresent = false;

    GameControllerKind latchedGamepadGuideFamily = GameControllerKind::Unknown;

    InputGuideSourceKind pendingOwnerIdeal = InputGuideSourceKind::Unknown;
    DWORD pendingOwnerSinceTick = 0;

    InputGuideSourceKind logPrevOwner = InputGuideSourceKind::Unknown;
    GameControllerKind logPrevGuide = GameControllerKind::Unknown;
    GameControllerKind logPrevInventoryFamily = GameControllerKind::Unknown;
    bool logPrevInventoryPresent = false;

    DWORD lastInventoryGuideKeptLogTick = 0;
};

SinglePlayerGuideArbiter g_sp;

void LogOwnerCommitIfChanged(InputGuideSourceKind prev, InputGuideSourceKind now)
{
    if (!kT76DebugLog || prev == now)
    {
        return;
    }
    g_sp.logPrevOwner = now;
    wchar_t line[200] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T76] owner commit %s -> %s\r\n",
        SourceKindLabel(prev),
        SourceKindLabel(now));
    OutputDebugStringW(line);
}

void LogGuideFamilyIfChanged(GameControllerKind g)
{
    if (!kT76DebugLog || g == g_sp.logPrevGuide)
    {
        return;
    }
    g_sp.logPrevGuide = g;
    wchar_t line[192] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T76] effective guide family -> %s\r\n",
        FamilyShortLabel(g));
    OutputDebugStringW(line);
}

void LogInventoryIfChanged()
{
    if (!kT76DebugLog)
    {
        return;
    }
    const bool invSame = (g_sp.inventoryPadFamily == g_sp.logPrevInventoryFamily) &&
        (g_sp.inventoryPadPresent == g_sp.logPrevInventoryPresent);
    if (invSame)
    {
        return;
    }
    g_sp.logPrevInventoryFamily = g_sp.inventoryPadFamily;
    g_sp.logPrevInventoryPresent = g_sp.inventoryPadPresent;
    wchar_t line[224] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T76] inventory: pad_present=%d pad_family=%s\r\n",
        g_sp.inventoryPadPresent ? 1 : 0,
        FamilyShortLabel(g_sp.inventoryPadFamily));
    OutputDebugStringW(line);
}

GameControllerKind EffectiveGuideFromOwner()
{
    if (g_sp.ownerSource != InputGuideSourceKind::Gamepad)
    {
        return GameControllerKind::Unknown;
    }
    if (g_sp.latchedGamepadGuideFamily != GameControllerKind::Unknown)
    {
        return g_sp.latchedGamepadGuideFamily;
    }
    return g_sp.inventoryPadFamily;
}

void ReconcileOwnerWithInventoryPresence()
{
    if (g_sp.ownerSource != InputGuideSourceKind::Gamepad)
    {
        return;
    }
    if (g_sp.inventoryPadPresent)
    {
        return;
    }
    const InputGuideSourceKind prevOwner = g_sp.ownerSource;
    g_sp.ownerSource = InputGuideSourceKind::Keyboard;
    g_sp.pendingOwnerIdeal = InputGuideSourceKind::Unknown;
    g_sp.pendingOwnerSinceTick = 0;
    LogOwnerCommitIfChanged(prevOwner, g_sp.ownerSource);
    const GameControllerKind g = EffectiveGuideFromOwner();
    LogGuideFamilyIfChanged(g);
}

void MaybeLogInventoryVersusLatchedGuide(GameControllerKind prevInvFamily)
{
    if (!kT76DebugLog)
    {
        return;
    }
    if (!g_sp.inventoryPadPresent)
    {
        return;
    }
    if (prevInvFamily == g_sp.inventoryPadFamily)
    {
        return;
    }
    if (g_sp.ownerSource != InputGuideSourceKind::Gamepad)
    {
        return;
    }
    if (g_sp.latchedGamepadGuideFamily == GameControllerKind::Unknown)
    {
        return;
    }
    if (g_sp.latchedGamepadGuideFamily == g_sp.inventoryPadFamily)
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (g_sp.lastInventoryGuideKeptLogTick != 0u &&
        ElapsedMs(now, g_sp.lastInventoryGuideKeptLogTick) < 2000u)
    {
        return;
    }
    g_sp.lastInventoryGuideKeptLogTick = now;
    wchar_t line[280] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T76] inventory family changed; guide latched kept (inv=%s latched=%s)\r\n",
        FamilyShortLabel(g_sp.inventoryPadFamily),
        FamilyShortLabel(g_sp.latchedGamepadGuideFamily));
    OutputDebugStringW(line);
}
} // namespace

void InputGuideArbiter_OnDeviceInventoryRefreshed(
    GameControllerKind inventoryGamepadFamily,
    bool hidGamepadFound,
    int xinputSlot)
{
    const GameControllerKind prevInv = g_sp.inventoryPadFamily;
    g_sp.inventoryPadFamily = inventoryGamepadFamily;
    g_sp.inventoryPadPresent = hidGamepadFound || (xinputSlot >= 0);
    LogInventoryIfChanged();
    MaybeLogInventoryVersusLatchedGuide(prevInv);
    if (!g_sp.inventoryPadPresent)
    {
        g_sp.latchedGamepadGuideFamily = GameControllerKind::Unknown;
    }
    ReconcileOwnerWithInventoryPresence();
    const GameControllerKind g = EffectiveGuideFromOwner();
    LogGuideFamilyIfChanged(g);
}

void InputGuideArbiter_TickSinglePlayerFromConsumerFrames(
    const VirtualInputConsumerFrame& keyboardFrame,
    const VirtualInputConsumerFrame& gamepadFrame,
    GameControllerKind gamepadGuideFamilyHintOnActivity)
{
    const DWORD now = GetTickCount();
    const bool kb = ConsumerFrameHasMeaningfulAction(keyboardFrame);
    const bool pad = ConsumerFrameHasMeaningfulAction(gamepadFrame);

    const InputGuideSourceKind rawIdeal = ComputeRawIdealOwner(kb, pad, g_sp.ownerSource);

    bool shouldCommit = false;
    if (g_sp.ownerSource == InputGuideSourceKind::Unknown &&
        rawIdeal != InputGuideSourceKind::Unknown)
    {
        shouldCommit = true;
    }
    else if (rawIdeal != g_sp.ownerSource)
    {
        if (g_sp.pendingOwnerIdeal != rawIdeal)
        {
            g_sp.pendingOwnerIdeal = rawIdeal;
            g_sp.pendingOwnerSinceTick = now;
        }
        else if (ElapsedMs(now, g_sp.pendingOwnerSinceTick) >= kOwnerIdealStableMs)
        {
            shouldCommit = true;
        }
    }
    else
    {
        g_sp.pendingOwnerIdeal = InputGuideSourceKind::Unknown;
        g_sp.pendingOwnerSinceTick = 0u;
    }

    if (shouldCommit)
    {
        const InputGuideSourceKind prev = g_sp.ownerSource;
        g_sp.ownerSource = rawIdeal;
        g_sp.pendingOwnerIdeal = InputGuideSourceKind::Unknown;
        g_sp.pendingOwnerSinceTick = 0u;
        LogOwnerCommitIfChanged(prev, g_sp.ownerSource);
        if (g_sp.ownerSource == InputGuideSourceKind::Keyboard)
        {
            g_sp.latchedGamepadGuideFamily = GameControllerKind::Unknown;
        }
        if (g_sp.ownerSource == InputGuideSourceKind::Gamepad &&
            g_sp.latchedGamepadGuideFamily == GameControllerKind::Unknown)
        {
            if (gamepadGuideFamilyHintOnActivity != GameControllerKind::Unknown)
            {
                g_sp.latchedGamepadGuideFamily = gamepadGuideFamilyHintOnActivity;
            }
            else if (g_sp.inventoryPadFamily != GameControllerKind::Unknown)
            {
                g_sp.latchedGamepadGuideFamily = g_sp.inventoryPadFamily;
            }
        }
    }

    if (g_sp.ownerSource == InputGuideSourceKind::Gamepad && pad &&
        gamepadGuideFamilyHintOnActivity != GameControllerKind::Unknown)
    {
        if (g_sp.latchedGamepadGuideFamily != gamepadGuideFamilyHintOnActivity)
        {
            g_sp.latchedGamepadGuideFamily = gamepadGuideFamilyHintOnActivity;
        }
    }

    const GameControllerKind g = EffectiveGuideFromOwner();
    LogGuideFamilyIfChanged(g);
}

InputGuideSourceKind InputGuideArbiter_GetEffectiveOwnerSourceKind()
{
    return g_sp.ownerSource;
}

GameControllerKind InputGuideArbiter_GetEffectiveGuideFamilyForUi()
{
    return EffectiveGuideFromOwner();
}
