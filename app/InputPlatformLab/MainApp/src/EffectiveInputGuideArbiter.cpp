// T76: device inventory (T18) vs UI guide / effective owner. T77: primary state in g_playerSlots[0] (1P only).

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

constexpr PlayerInputSlotIndex kT76PrimaryPlayerSlotIndex = 0u;

PlayerSlotState g_playerSlots[kPlayerInputSlotCap];

void EnsurePrimaryPlayerSlotSeededForT76()
{
    static bool s_once = false;
    if (s_once)
    {
        return;
    }
    s_once = true;
    PlayerSlotState& s = g_playerSlots[kT76PrimaryPlayerSlotIndex];
    s.slotAssigned = true;
    s.slotActive = true;
    // T77 step2: 1P seat is active; device instance stays unbound until explicit bind / routing.
    s.bindingAssignment = PlayerSlotBindingAssignment::ActiveOpen;
    s.boundSourceKind = InputGuideSourceKind::Unknown;
    s.boundDeviceIdentity = PlayerBoundDeviceIdentity{};
    s.boundDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::Unbound;
    s.preferredGuideFamily = GameControllerKind::Unknown;
    s.lastSeenSourceMeta = PlayerSlotLastSeenSourceMeta{};
}

PlayerSlotState& PrimarySlot()
{
    return g_playerSlots[kT76PrimaryPlayerSlotIndex];
}

PlayerSlotState* TryMutableSlot(PlayerInputSlotIndex slot)
{
    if (static_cast<unsigned>(slot) >= kPlayerInputSlotCap)
    {
        return nullptr;
    }
    return &g_playerSlots[slot];
}

void ResolveOnePlayerSlot(PlayerSlotState& s, const PlayerInputInventoryBindingView& inv, UINT32 tick)
{
    PlayerSlotBindingResolution& r = s.bindingResolution;
    r.lastResolvedTick = tick;
    r.lastInventoryMatchIdentity = PlayerBoundDeviceIdentity{};

    if (s.bindingAssignment == PlayerSlotBindingAssignment::None)
    {
        r.status = PlayerSlotBindingResolveStatus::IdleNoPolicy;
        r.boundDevicePresent = false;
        r.resolvedSourceKind = InputGuideSourceKind::Unknown;
        r.resolvedGuideFamilyCandidate = GameControllerKind::Unknown;
        return;
    }

    if (s.bindingAssignment == PlayerSlotBindingAssignment::ActiveOpen)
    {
        r.status = PlayerSlotBindingResolveStatus::OpenNoLock;
        r.boundDevicePresent = false;
        r.resolvedSourceKind = InputGuideSourceKind::Unknown;
        r.resolvedGuideFamilyCandidate = (s.preferredGuideFamily != GameControllerKind::Unknown)
            ? s.preferredGuideFamily
            : inv.inventoryPrimaryPadFamily;
        return;
    }

    if (s.boundSourceKind == InputGuideSourceKind::Keyboard ||
        s.boundDeviceIdentity.kind == PlayerBoundDeviceIdentityKind::Keyboard)
    {
        r.status = PlayerSlotBindingResolveStatus::LockedPresent;
        r.boundDevicePresent = true;
        r.resolvedSourceKind = InputGuideSourceKind::Keyboard;
        r.resolvedGuideFamilyCandidate = GameControllerKind::Unknown;
        r.lastInventoryMatchIdentity.kind = PlayerBoundDeviceIdentityKind::Keyboard;
        return;
    }

    if (s.boundDeviceIdentity.kind == PlayerBoundDeviceIdentityKind::XInputUser)
    {
        const INT32 n = s.boundDeviceIdentity.xinputUserIndex;
        const bool ok = n >= 0 && n < 4 && inv.xinputUserConnected[static_cast<size_t>(n)];
        r.boundDevicePresent = ok;
        r.status = ok ? PlayerSlotBindingResolveStatus::LockedPresent : PlayerSlotBindingResolveStatus::LockedAbsent;
        r.resolvedSourceKind = InputGuideSourceKind::Gamepad;
        r.resolvedGuideFamilyCandidate = ok ? inv.inventoryPrimaryPadFamily : GameControllerKind::Unknown;
        if (ok)
        {
            r.lastInventoryMatchIdentity.kind = PlayerBoundDeviceIdentityKind::XInputUser;
            r.lastInventoryMatchIdentity.xinputUserIndex = n;
        }
        return;
    }

    if (s.boundDeviceIdentity.kind == PlayerBoundDeviceIdentityKind::HidPathHash)
    {
        const bool hashMatch = inv.inventoryHidRowPresent &&
            s.boundDeviceIdentity.hidInstancePathHash != 0u &&
            s.boundDeviceIdentity.hidInstancePathHash == inv.inventoryHidPathFnv1a;
        const bool tokenMatch = inv.inventoryHidRowPresent &&
            s.boundDeviceIdentity.hidPathShortToken != 0u &&
            s.boundDeviceIdentity.hidPathShortToken == inv.inventoryHidPathToken16;
        const bool vidPidMatch = inv.inventoryHidRowPresent &&
            s.boundDeviceIdentity.vendorId != 0u &&
            s.boundDeviceIdentity.vendorId == inv.inventoryHidVendorId &&
            s.boundDeviceIdentity.productId == inv.inventoryHidProductId;
        const bool present = hashMatch || tokenMatch || vidPidMatch;
        r.boundDevicePresent = present;
        r.status =
            present ? PlayerSlotBindingResolveStatus::LockedPresent : PlayerSlotBindingResolveStatus::LockedAbsent;
        r.resolvedSourceKind = InputGuideSourceKind::Gamepad;
        r.resolvedGuideFamilyCandidate = present ? inv.inventoryPrimaryPadFamily : GameControllerKind::Unknown;
        if (present)
        {
            r.lastInventoryMatchIdentity = s.boundDeviceIdentity;
            r.lastInventoryMatchIdentity.hidInstancePathHash = inv.inventoryHidPathFnv1a;
            r.lastInventoryMatchIdentity.hidPathShortToken = inv.inventoryHidPathToken16;
        }
        return;
    }

    r.boundDevicePresent = false;
    r.status = PlayerSlotBindingResolveStatus::LockedAbsent;
    r.resolvedSourceKind = s.boundSourceKind;
    r.resolvedGuideFamilyCandidate = GameControllerKind::Unknown;
}

int FirstConnectedXInputUserIndex(const PlayerInputInventoryBindingView& inv)
{
    for (int i = 0; i < 4; ++i)
    {
        if (inv.xinputUserConnected[static_cast<size_t>(i)])
        {
            return i;
        }
    }
    return -1;
}

void ComputeOneSlotRouteCandidate(PlayerSlotState& s, const PlayerInputInventoryBindingView& inv, UINT32 tick)
{
    PlayerSlotRouteCandidate& c = s.routeCandidate;
    const PlayerSlotBindingResolution& r = s.bindingResolution;
    c.lastCandidateTick = tick;
    c.readiness = PlayerSlotRouteReadiness::None;
    c.mode = PlayerSlotRouteCandidateMode::None;
    c.candidateSourceKind = InputGuideSourceKind::Unknown;
    c.candidateDeviceIdentity = PlayerBoundDeviceIdentity{};
    c.candidateResolvedFamily = GameControllerKind::Unknown;

    if (s.bindingAssignment == PlayerSlotBindingAssignment::None)
    {
        return;
    }

    if (r.status == PlayerSlotBindingResolveStatus::Unresolved)
    {
        return;
    }

    if (s.bindingAssignment == PlayerSlotBindingAssignment::ActiveOpen)
    {
        c.readiness = PlayerSlotRouteReadiness::OpenReady;
        c.mode = PlayerSlotRouteCandidateMode::OpenSoftInventory;
        const int xi = FirstConnectedXInputUserIndex(inv);
        const bool padInventory = xi >= 0 || inv.inventoryHidRowPresent;
        if (padInventory)
        {
            c.candidateSourceKind = InputGuideSourceKind::Gamepad;
            c.candidateResolvedFamily = inv.inventoryPrimaryPadFamily;
            if (xi >= 0)
            {
                c.candidateDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::XInputUser;
                c.candidateDeviceIdentity.xinputUserIndex = xi;
            }
            else
            {
                c.candidateDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::HidPathHash;
                c.candidateDeviceIdentity.hidInstancePathHash = inv.inventoryHidPathFnv1a;
                c.candidateDeviceIdentity.hidPathShortToken = inv.inventoryHidPathToken16;
                c.candidateDeviceIdentity.vendorId = inv.inventoryHidVendorId;
                c.candidateDeviceIdentity.productId = inv.inventoryHidProductId;
            }
        }
        else
        {
            c.candidateSourceKind = InputGuideSourceKind::Keyboard;
            c.candidateDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::Keyboard;
        }
        return;
    }

    if (r.status != PlayerSlotBindingResolveStatus::LockedPresent)
    {
        return;
    }

    c.readiness = PlayerSlotRouteReadiness::Ready;

    if (s.boundSourceKind == InputGuideSourceKind::Keyboard ||
        s.boundDeviceIdentity.kind == PlayerBoundDeviceIdentityKind::Keyboard)
    {
        c.mode = PlayerSlotRouteCandidateMode::LockedKeyboard;
        c.candidateSourceKind = InputGuideSourceKind::Keyboard;
        c.candidateDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::Keyboard;
        return;
    }

    if (s.boundDeviceIdentity.kind == PlayerBoundDeviceIdentityKind::XInputUser)
    {
        c.mode = PlayerSlotRouteCandidateMode::LockedXInputUser;
        c.candidateSourceKind = InputGuideSourceKind::Gamepad;
        c.candidateDeviceIdentity = s.boundDeviceIdentity;
        c.candidateResolvedFamily = r.resolvedGuideFamilyCandidate;
        return;
    }

    if (s.boundDeviceIdentity.kind == PlayerBoundDeviceIdentityKind::HidPathHash)
    {
        c.mode = PlayerSlotRouteCandidateMode::LockedHidPath;
        c.candidateSourceKind = InputGuideSourceKind::Gamepad;
        if (r.lastInventoryMatchIdentity.hidInstancePathHash != 0u ||
            r.lastInventoryMatchIdentity.vendorId != 0u)
        {
            c.candidateDeviceIdentity = r.lastInventoryMatchIdentity;
        }
        else
        {
            c.candidateDeviceIdentity = s.boundDeviceIdentity;
            c.candidateDeviceIdentity.hidInstancePathHash = inv.inventoryHidPathFnv1a;
            c.candidateDeviceIdentity.hidPathShortToken = inv.inventoryHidPathToken16;
        }
        c.candidateResolvedFamily = r.resolvedGuideFamilyCandidate;
        return;
    }

    c.readiness = PlayerSlotRouteReadiness::None;
    c.mode = PlayerSlotRouteCandidateMode::None;
}

void ResolveAllSlotBindingsFromInventoryView(const PlayerInputInventoryBindingView& inv)
{
    EnsurePrimaryPlayerSlotSeededForT76();
    const UINT32 tick = static_cast<UINT32>(GetTickCount());
    for (unsigned i = 0; i < kPlayerInputSlotCap; ++i)
    {
        ResolveOnePlayerSlot(g_playerSlots[i], inv, tick);
        ComputeOneSlotRouteCandidate(g_playerSlots[i], inv, tick);
    }
}

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

void LogOwnerCommitIfChanged(InputGuideSourceKind prev, InputGuideSourceKind now)
{
    if (!kT76DebugLog || prev == now)
    {
        return;
    }
    PrimarySlot().logPrevOwner = now;
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
    if (!kT76DebugLog || g == PrimarySlot().logPrevGuide)
    {
        return;
    }
    PrimarySlot().logPrevGuide = g;
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
    PlayerSlotState& s = PrimarySlot();
    const bool invSame = (s.inventoryPadFamily == s.logPrevInventoryFamily) &&
        (s.inventoryPadPresent == s.logPrevInventoryPresent);
    if (invSame)
    {
        return;
    }
    s.logPrevInventoryFamily = s.inventoryPadFamily;
    s.logPrevInventoryPresent = s.inventoryPadPresent;
    wchar_t line[224] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T76] inventory: pad_present=%d pad_family=%s\r\n",
        s.inventoryPadPresent ? 1 : 0,
        FamilyShortLabel(s.inventoryPadFamily));
    OutputDebugStringW(line);
}

GameControllerKind EffectiveGuideFromOwner()
{
    PlayerSlotState& s = PrimarySlot();
    if (s.effectiveOwnerSource != InputGuideSourceKind::Gamepad)
    {
        return GameControllerKind::Unknown;
    }
    if (s.latchedGamepadGuideFamily != GameControllerKind::Unknown)
    {
        return s.latchedGamepadGuideFamily;
    }
    return s.inventoryPadFamily;
}

void ReconcileOwnerWithInventoryPresence()
{
    PlayerSlotState& s = PrimarySlot();
    if (s.effectiveOwnerSource != InputGuideSourceKind::Gamepad)
    {
        return;
    }
    if (s.inventoryPadPresent)
    {
        return;
    }
    const InputGuideSourceKind prevOwner = s.effectiveOwnerSource;
    s.effectiveOwnerSource = InputGuideSourceKind::Keyboard;
    s.pendingOwnerIdeal = InputGuideSourceKind::Unknown;
    s.pendingOwnerSinceTick = 0;
    LogOwnerCommitIfChanged(prevOwner, s.effectiveOwnerSource);
    const GameControllerKind g = EffectiveGuideFromOwner();
    LogGuideFamilyIfChanged(g);
}

void MaybeLogInventoryVersusLatchedGuide(GameControllerKind prevInvFamily)
{
    if (!kT76DebugLog)
    {
        return;
    }
    PlayerSlotState& s = PrimarySlot();
    if (!s.inventoryPadPresent)
    {
        return;
    }
    if (prevInvFamily == s.inventoryPadFamily)
    {
        return;
    }
    if (s.effectiveOwnerSource != InputGuideSourceKind::Gamepad)
    {
        return;
    }
    if (s.latchedGamepadGuideFamily == GameControllerKind::Unknown)
    {
        return;
    }
    if (s.latchedGamepadGuideFamily == s.inventoryPadFamily)
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (s.lastInventoryGuideKeptLogTick != 0u &&
        ElapsedMs(now, s.lastInventoryGuideKeptLogTick) < 2000u)
    {
        return;
    }
    s.lastInventoryGuideKeptLogTick = now;
    wchar_t line[280] = {};
    swprintf_s(
        line,
        _countof(line),
        L"[T76] inventory family changed; guide latched kept (inv=%s latched=%s)\r\n",
        FamilyShortLabel(s.inventoryPadFamily),
        FamilyShortLabel(s.latchedGamepadGuideFamily));
    OutputDebugStringW(line);
}

void FillSlotBoundSourceLineForT18(const PlayerSlotState& s, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    switch (s.bindingAssignment)
    {
    case PlayerSlotBindingAssignment::None:
        wcscpy_s(buf, bufCount, L"none");
        break;
    case PlayerSlotBindingAssignment::ActiveOpen:
        if (s.boundSourceKind == InputGuideSourceKind::Unknown)
        {
            wcscpy_s(buf, bufCount, L"open(any)");
        }
        else
        {
            swprintf_s(buf, bufCount, L"open+%ls", SourceKindLabel(s.boundSourceKind));
        }
        break;
    case PlayerSlotBindingAssignment::BoundLocked:
        swprintf_s(buf, bufCount, L"locked:%ls", SourceKindLabel(s.boundSourceKind));
        break;
    default:
        wcscpy_s(buf, bufCount, L"unknown");
        break;
    }
}

void FillSlotBoundDeviceIdentityLineForT18(const PlayerBoundDeviceIdentity& id, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    switch (id.kind)
    {
    case PlayerBoundDeviceIdentityKind::Unbound:
        wcscpy_s(buf, bufCount, L"unbound");
        break;
    case PlayerBoundDeviceIdentityKind::Keyboard:
        wcscpy_s(buf, bufCount, L"keyboard");
        break;
    case PlayerBoundDeviceIdentityKind::XInputUser:
        if (id.xinputUserIndex >= 0)
        {
            swprintf_s(buf, bufCount, L"XInput user %d", id.xinputUserIndex);
        }
        else
        {
            wcscpy_s(buf, bufCount, L"XInput (slot unspecified)");
        }
        break;
    case PlayerBoundDeviceIdentityKind::HidPathHash:
        if (id.hidInstancePathHash != 0u)
        {
            swprintf_s(
                buf,
                bufCount,
                L"HID#%08X",
                static_cast<unsigned>(id.hidInstancePathHash));
        }
        else if (id.hidPathShortToken != 0u)
        {
            swprintf_s(buf, bufCount, L"HID*#%04X", static_cast<unsigned>(id.hidPathShortToken));
        }
        else
        {
            wcscpy_s(buf, bufCount, L"HID (unset hash)");
        }
        break;
    case PlayerBoundDeviceIdentityKind::Unknown:
    default:
        wcscpy_s(buf, bufCount, L"unknown");
        break;
    }
}

void FillPrimarySlotBoundSourceLineForT18(wchar_t* buf, size_t bufCount)
{
    FillSlotBoundSourceLineForT18(PrimarySlot(), buf, bufCount);
}

void FillPrimarySlotBoundDeviceIdentityLineForT18(wchar_t* buf, size_t bufCount)
{
    FillSlotBoundDeviceIdentityLineForT18(PrimarySlot().boundDeviceIdentity, buf, bufCount);
}

void FillSlotBindStatusLineForT18(const PlayerSlotState& s, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    switch (s.bindingResolution.status)
    {
    case PlayerSlotBindingResolveStatus::Unresolved:
        wcscpy_s(buf, bufCount, L"unresolved");
        break;
    case PlayerSlotBindingResolveStatus::IdleNoPolicy:
        wcscpy_s(buf, bufCount, L"none");
        break;
    case PlayerSlotBindingResolveStatus::OpenNoLock:
        wcscpy_s(buf, bufCount, L"open");
        break;
    case PlayerSlotBindingResolveStatus::LockedPresent:
        wcscpy_s(buf, bufCount, L"locked,present");
        break;
    case PlayerSlotBindingResolveStatus::LockedAbsent:
        wcscpy_s(buf, bufCount, L"locked,absent");
        break;
    default:
        wcscpy_s(buf, bufCount, L"unknown");
        break;
    }
}

void FillSlotBindMatchLineForT18(const PlayerSlotState& s, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    switch (s.bindingAssignment)
    {
    case PlayerSlotBindingAssignment::None:
        wcscpy_s(buf, bufCount, L"none");
        break;
    case PlayerSlotBindingAssignment::ActiveOpen:
        wcscpy_s(buf, bufCount, L"open(any)");
        break;
    case PlayerSlotBindingAssignment::BoundLocked:
    {
        const PlayerBoundDeviceIdentity& id = s.boundDeviceIdentity;
        switch (id.kind)
        {
        case PlayerBoundDeviceIdentityKind::Keyboard:
            wcscpy_s(buf, bufCount, L"Keyboard");
            break;
        case PlayerBoundDeviceIdentityKind::XInputUser:
            if (id.xinputUserIndex >= 0)
            {
                swprintf_s(buf, bufCount, L"XInput user %d", id.xinputUserIndex);
            }
            else
            {
                wcscpy_s(buf, bufCount, L"XInput (unspecified)");
            }
            break;
        case PlayerBoundDeviceIdentityKind::HidPathHash:
            if (id.hidInstancePathHash != 0u)
            {
                swprintf_s(
                    buf,
                    bufCount,
                    L"HID#%08X",
                    static_cast<unsigned>(id.hidInstancePathHash));
            }
            else if (id.hidPathShortToken != 0u)
            {
                swprintf_s(buf, bufCount, L"HID*#%04X", static_cast<unsigned>(id.hidPathShortToken));
            }
            else if (id.vendorId != 0u)
            {
                swprintf_s(
                    buf,
                    bufCount,
                    L"VID/PID %04X/%04X",
                    static_cast<unsigned>(id.vendorId),
                    static_cast<unsigned>(id.productId));
            }
            else
            {
                wcscpy_s(buf, bufCount, L"HID bind");
            }
            break;
        case PlayerBoundDeviceIdentityKind::Unbound:
            if (s.boundSourceKind == InputGuideSourceKind::Keyboard)
            {
                wcscpy_s(buf, bufCount, L"Keyboard");
            }
            else if (s.boundSourceKind == InputGuideSourceKind::Gamepad)
            {
                wcscpy_s(buf, bufCount, L"Gamepad (no instance)");
            }
            else
            {
                wcscpy_s(buf, bufCount, L"unbound");
            }
            break;
        case PlayerBoundDeviceIdentityKind::Unknown:
        default:
            swprintf_s(buf, bufCount, L"%ls", SourceKindLabel(s.boundSourceKind));
            break;
        }
        break;
    }
    default:
        wcscpy_s(buf, bufCount, L"unknown");
        break;
    }
}

void FillSlotRouteCandidateLineForT18(const PlayerSlotState& s, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    const PlayerSlotRouteCandidate& c = s.routeCandidate;
    switch (c.readiness)
    {
    case PlayerSlotRouteReadiness::None:
        wcscpy_s(buf, bufCount, L"none");
        break;
    case PlayerSlotRouteReadiness::OpenReady:
        wcscpy_s(buf, bufCount, L"open-ready");
        break;
    case PlayerSlotRouteReadiness::Ready:
        switch (c.mode)
        {
        case PlayerSlotRouteCandidateMode::LockedKeyboard:
            wcscpy_s(buf, bufCount, L"keyboard-ready");
            break;
        case PlayerSlotRouteCandidateMode::LockedXInputUser:
            if (c.candidateDeviceIdentity.xinputUserIndex >= 0)
            {
                swprintf_s(
                    buf,
                    bufCount,
                    L"xinput%d-ready",
                    c.candidateDeviceIdentity.xinputUserIndex);
            }
            else
            {
                wcscpy_s(buf, bufCount, L"xinput-ready");
            }
            break;
        case PlayerSlotRouteCandidateMode::LockedHidPath:
            wcscpy_s(buf, bufCount, L"hid-ready");
            break;
        default:
            wcscpy_s(buf, bufCount, L"ready");
            break;
        }
        break;
    default:
        wcscpy_s(buf, bufCount, L"none");
        break;
    }
}
} // namespace

void InputGuideArbiter_OnDeviceInventoryRefreshed(
    GameControllerKind inventoryGamepadFamily,
    bool hidGamepadFound,
    int xinputSlot)
{
    EnsurePrimaryPlayerSlotSeededForT76();
    PlayerSlotState& s = PrimarySlot();
    const GameControllerKind prevInv = s.inventoryPadFamily;
    s.inventoryPadFamily = inventoryGamepadFamily;
    s.inventoryPadPresent = hidGamepadFound || (xinputSlot >= 0);
    LogInventoryIfChanged();
    MaybeLogInventoryVersusLatchedGuide(prevInv);
    if (!s.inventoryPadPresent)
    {
        s.latchedGamepadGuideFamily = GameControllerKind::Unknown;
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
    EnsurePrimaryPlayerSlotSeededForT76();
    PlayerSlotState& s = PrimarySlot();
    const DWORD now = GetTickCount();
    const bool kb = ConsumerFrameHasMeaningfulAction(keyboardFrame);
    const bool pad = ConsumerFrameHasMeaningfulAction(gamepadFrame);

    const InputGuideSourceKind rawIdeal = ComputeRawIdealOwner(kb, pad, s.effectiveOwnerSource);

    bool shouldCommit = false;
    if (s.effectiveOwnerSource == InputGuideSourceKind::Unknown &&
        rawIdeal != InputGuideSourceKind::Unknown)
    {
        shouldCommit = true;
    }
    else if (rawIdeal != s.effectiveOwnerSource)
    {
        if (s.pendingOwnerIdeal != rawIdeal)
        {
            s.pendingOwnerIdeal = rawIdeal;
            s.pendingOwnerSinceTick = now;
        }
        else if (ElapsedMs(now, s.pendingOwnerSinceTick) >= kOwnerIdealStableMs)
        {
            shouldCommit = true;
        }
    }
    else
    {
        // rawIdeal == owner: idle. Keyboard consumer actions are often 1-tick edges; pad idle still
        // reads as Gamepad — do not clear a pending switch to the opposite source (T76 close).
        if (s.pendingOwnerIdeal == InputGuideSourceKind::Unknown ||
            s.pendingOwnerIdeal == s.effectiveOwnerSource)
        {
            s.pendingOwnerIdeal = InputGuideSourceKind::Unknown;
            s.pendingOwnerSinceTick = 0u;
        }
    }

    if (shouldCommit)
    {
        const InputGuideSourceKind prev = s.effectiveOwnerSource;
        s.effectiveOwnerSource = rawIdeal;
        s.pendingOwnerIdeal = InputGuideSourceKind::Unknown;
        s.pendingOwnerSinceTick = 0u;
        LogOwnerCommitIfChanged(prev, s.effectiveOwnerSource);
        if (s.effectiveOwnerSource == InputGuideSourceKind::Keyboard)
        {
            s.latchedGamepadGuideFamily = GameControllerKind::Unknown;
        }
        if (s.effectiveOwnerSource == InputGuideSourceKind::Gamepad &&
            s.latchedGamepadGuideFamily == GameControllerKind::Unknown)
        {
            if (gamepadGuideFamilyHintOnActivity != GameControllerKind::Unknown)
            {
                s.latchedGamepadGuideFamily = gamepadGuideFamilyHintOnActivity;
            }
            else if (s.inventoryPadFamily != GameControllerKind::Unknown)
            {
                s.latchedGamepadGuideFamily = s.inventoryPadFamily;
            }
        }
    }

    if (s.effectiveOwnerSource == InputGuideSourceKind::Gamepad && pad &&
        gamepadGuideFamilyHintOnActivity != GameControllerKind::Unknown)
    {
        if (s.latchedGamepadGuideFamily != gamepadGuideFamilyHintOnActivity)
        {
            s.latchedGamepadGuideFamily = gamepadGuideFamilyHintOnActivity;
        }
    }

    const GameControllerKind g = EffectiveGuideFromOwner();
    LogGuideFamilyIfChanged(g);

    // T77 step2: last-seen metadata only (does not affect T76 owner/guide commit).
    const UINT32 tickU32 = static_cast<UINT32>(now);
    if (kb)
    {
        s.lastSeenSourceMeta.sourceKind = InputGuideSourceKind::Keyboard;
        s.lastSeenSourceMeta.deviceKind = PlayerBoundDeviceIdentityKind::Keyboard;
        s.lastSeenSourceMeta.atTick = tickU32;
    }
    if (pad)
    {
        s.lastSeenSourceMeta.sourceKind = InputGuideSourceKind::Gamepad;
        s.lastSeenSourceMeta.deviceKind = PlayerBoundDeviceIdentityKind::Unknown;
        s.lastSeenSourceMeta.atTick = tickU32;
    }
}

InputGuideSourceKind InputGuideArbiter_GetEffectiveOwnerSourceKind()
{
    EnsurePrimaryPlayerSlotSeededForT76();
    return PrimarySlot().effectiveOwnerSource;
}

GameControllerKind InputGuideArbiter_GetEffectiveGuideFamilyForUi()
{
    EnsurePrimaryPlayerSlotSeededForT76();
    return EffectiveGuideFromOwner();
}

void InputGuideArbiter_FormatPrimarySlotBoundSourceForT18(wchar_t* buf, size_t bufCount)
{
    EnsurePrimaryPlayerSlotSeededForT76();
    FillPrimarySlotBoundSourceLineForT18(buf, bufCount);
}

void InputGuideArbiter_FormatPrimarySlotBoundDeviceIdentityForT18(wchar_t* buf, size_t bufCount)
{
    EnsurePrimaryPlayerSlotSeededForT76();
    FillPrimarySlotBoundDeviceIdentityLineForT18(buf, bufCount);
}

bool InputGuideArbiter_IsValidSlotIndex(PlayerInputSlotIndex slot)
{
    return static_cast<unsigned>(slot) < kPlayerInputSlotCap;
}

void InputGuideArbiter_SetSlotBindingAssignment(PlayerInputSlotIndex slot, PlayerSlotBindingAssignment assignment)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->bindingAssignment = assignment;
}

void InputGuideArbiter_SetSlotBoundSourceKind(PlayerInputSlotIndex slot, InputGuideSourceKind kind)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->boundSourceKind = kind;
}

void InputGuideArbiter_SetSlotBoundDeviceIdentity(PlayerInputSlotIndex slot, const PlayerBoundDeviceIdentity& identity)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->boundDeviceIdentity = identity;
}

void InputGuideArbiter_SetSlotPreferredGuideFamily(PlayerInputSlotIndex slot, GameControllerKind family)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->preferredGuideFamily = family;
}

void InputGuideArbiter_SetSlotPartySeatFlags(PlayerInputSlotIndex slot, bool assigned, bool active)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->slotAssigned = assigned;
    s->slotActive = active;
}

void InputGuideArbiter_ClearSlotBindingPolicy(PlayerInputSlotIndex slot)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    if (slot == kT76PrimaryPlayerSlotIndex)
    {
        s->bindingAssignment = PlayerSlotBindingAssignment::ActiveOpen;
        s->boundSourceKind = InputGuideSourceKind::Unknown;
        s->boundDeviceIdentity = PlayerBoundDeviceIdentity{};
        s->boundDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::Unbound;
        s->preferredGuideFamily = GameControllerKind::Unknown;
        return;
    }
    s->bindingAssignment = PlayerSlotBindingAssignment::None;
    s->boundSourceKind = InputGuideSourceKind::Unknown;
    s->boundDeviceIdentity = PlayerBoundDeviceIdentity{};
    s->preferredGuideFamily = GameControllerKind::Unknown;
    s->slotAssigned = false;
    s->slotActive = false;
}

void InputGuideArbiter_BindSlotToKeyboard(PlayerInputSlotIndex slot)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->bindingAssignment = PlayerSlotBindingAssignment::BoundLocked;
    s->boundSourceKind = InputGuideSourceKind::Keyboard;
    s->boundDeviceIdentity = PlayerBoundDeviceIdentity{};
    s->boundDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::Keyboard;
}

void InputGuideArbiter_BindSlotToXInputUser(PlayerInputSlotIndex slot, INT32 xinputUserIndex)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->bindingAssignment = PlayerSlotBindingAssignment::BoundLocked;
    s->boundSourceKind = InputGuideSourceKind::Gamepad;
    s->boundDeviceIdentity = PlayerBoundDeviceIdentity{};
    s->boundDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::XInputUser;
    s->boundDeviceIdentity.xinputUserIndex = xinputUserIndex;
}

void InputGuideArbiter_BindSlotToHidPathHash(
    PlayerInputSlotIndex slot,
    UINT32 hidPathHash,
    UINT16 hidPathShortToken,
    UINT16 vendorId,
    UINT16 productId)
{
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        return;
    }
    s->bindingAssignment = PlayerSlotBindingAssignment::BoundLocked;
    s->boundSourceKind = InputGuideSourceKind::Gamepad;
    s->boundDeviceIdentity = PlayerBoundDeviceIdentity{};
    s->boundDeviceIdentity.kind = PlayerBoundDeviceIdentityKind::HidPathHash;
    s->boundDeviceIdentity.hidInstancePathHash = hidPathHash;
    s->boundDeviceIdentity.hidPathShortToken = hidPathShortToken;
    s->boundDeviceIdentity.vendorId = vendorId;
    s->boundDeviceIdentity.productId = productId;
}

void InputGuideArbiter_ApplyStep3DemoReservationBindings()
{
    static bool s_done = false;
    if (s_done)
    {
        return;
    }
    s_done = true;
    EnsurePrimaryPlayerSlotSeededForT76();
    // 2P / 3P policy placeholders: no input routing; T76 still uses merged 1P stream only.
    InputGuideArbiter_SetSlotPartySeatFlags(1u, true, false);
    InputGuideArbiter_BindSlotToKeyboard(1u);
    InputGuideArbiter_SetSlotPartySeatFlags(2u, true, false);
    InputGuideArbiter_BindSlotToXInputUser(2u, 0);
}

void InputGuideArbiter_FormatSlotBoundSourceForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    EnsurePrimaryPlayerSlotSeededForT76();
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        wcscpy_s(buf, bufCount, L"invalid slot");
        return;
    }
    FillSlotBoundSourceLineForT18(*s, buf, bufCount);
}

void InputGuideArbiter_FormatSlotBoundDeviceIdentityForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    EnsurePrimaryPlayerSlotSeededForT76();
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        wcscpy_s(buf, bufCount, L"invalid slot");
        return;
    }
    FillSlotBoundDeviceIdentityLineForT18(s->boundDeviceIdentity, buf, bufCount);
}

void InputGuideArbiter_ResolveSlotBindingsFromInventory(const PlayerInputInventoryBindingView& inventory)
{
    ResolveAllSlotBindingsFromInventoryView(inventory);
}

void InputGuideArbiter_FormatSlotBindStatusForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    EnsurePrimaryPlayerSlotSeededForT76();
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        wcscpy_s(buf, bufCount, L"invalid slot");
        return;
    }
    FillSlotBindStatusLineForT18(*s, buf, bufCount);
}

void InputGuideArbiter_FormatSlotBindMatchForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    EnsurePrimaryPlayerSlotSeededForT76();
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        wcscpy_s(buf, bufCount, L"invalid slot");
        return;
    }
    FillSlotBindMatchLineForT18(*s, buf, bufCount);
}

void InputGuideArbiter_FormatSlotRouteCandidateForT18(PlayerInputSlotIndex slot, wchar_t* buf, size_t bufCount)
{
    if (!buf || bufCount == 0)
    {
        return;
    }
    buf[0] = L'\0';
    EnsurePrimaryPlayerSlotSeededForT76();
    PlayerSlotState* s = TryMutableSlot(slot);
    if (!s)
    {
        wcscpy_s(buf, bufCount, L"invalid slot");
        return;
    }
    FillSlotRouteCandidateLineForT18(*s, buf, bufCount);
}
