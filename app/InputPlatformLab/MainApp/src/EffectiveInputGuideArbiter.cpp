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

bool ConsumerFrameHasMeaningfulAction(const VirtualInputConsumerFrame& f)
{
    return f.confirmPressed || f.cancelPressed || f.menuPressed || f.moveX != 0 || f.moveY != 0;
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

    InputGuideSourceKind logPrevOwner = InputGuideSourceKind::Unknown;
    GameControllerKind logPrevGuide = GameControllerKind::Unknown;
    GameControllerKind logPrevInventoryFamily = GameControllerKind::Unknown;
    bool logPrevInventoryPresent = false;
};

SinglePlayerGuideArbiter g_sp;

void LogOwnerIfChanged(InputGuideSourceKind nowOwner)
{
    if (!kT76DebugLog || nowOwner == g_sp.logPrevOwner)
    {
        return;
    }
    g_sp.logPrevOwner = nowOwner;
    wchar_t line[160] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T76] effective owner source -> %s\r\n",
        SourceKindLabel(nowOwner));
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
    g_sp.ownerSource = InputGuideSourceKind::Keyboard;
    LogOwnerIfChanged(g_sp.ownerSource);
    const GameControllerKind g = EffectiveGuideFromOwner();
    LogGuideFamilyIfChanged(g);
    if (kT76DebugLog)
    {
        OutputDebugStringW(L"[T76] owner -> Keyboard (no gamepad in inventory)\r\n");
    }
}
} // namespace

void InputGuideArbiter_OnDeviceInventoryRefreshed(
    GameControllerKind inventoryGamepadFamily,
    bool hidGamepadFound,
    int xinputSlot)
{
    g_sp.inventoryPadFamily = inventoryGamepadFamily;
    g_sp.inventoryPadPresent = hidGamepadFound || (xinputSlot >= 0);
    LogInventoryIfChanged();
    ReconcileOwnerWithInventoryPresence();
}

void InputGuideArbiter_TickSinglePlayerFromConsumerFrames(
    const VirtualInputConsumerFrame& keyboardFrame,
    const VirtualInputConsumerFrame& gamepadFrame)
{
    const bool kb = ConsumerFrameHasMeaningfulAction(keyboardFrame);
    const bool pad = ConsumerFrameHasMeaningfulAction(gamepadFrame);

    if (kb && !pad)
    {
        g_sp.ownerSource = InputGuideSourceKind::Keyboard;
    }
    else if (pad && !kb)
    {
        g_sp.ownerSource = InputGuideSourceKind::Gamepad;
    }
    else if (kb && pad)
    {
        if (g_sp.ownerSource == InputGuideSourceKind::Unknown)
        {
            g_sp.ownerSource = InputGuideSourceKind::Keyboard;
        }
        // else: keep last owner (simultaneous input: no flip-flop)
    }

    LogOwnerIfChanged(g_sp.ownerSource);
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
