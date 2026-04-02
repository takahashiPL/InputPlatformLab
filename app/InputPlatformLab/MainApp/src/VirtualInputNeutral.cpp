#include "VirtualInputNeutral.h"

void VirtualInput_ResetDisconnected(VirtualInputSnapshot& s)
{
    s = {};
    s.connected = false;
    s.family = GameControllerKind::Unknown;
    s.leftInDeadzone = true;
    s.rightInDeadzone = true;
}

bool VirtualInput_IsButtonDown(const VirtualInputSnapshot& s, GamepadButtonId id)
{
    switch (id)
    {
    case GamepadButtonId::South: return s.south;
    case GamepadButtonId::East: return s.east;
    case GamepadButtonId::West: return s.west;
    case GamepadButtonId::North: return s.north;
    case GamepadButtonId::L1: return s.l1;
    case GamepadButtonId::R1: return s.r1;
    case GamepadButtonId::L2: return s.l2Pressed;
    case GamepadButtonId::R2: return s.r2Pressed;
    case GamepadButtonId::L3: return s.l3;
    case GamepadButtonId::R3: return s.r3;
    case GamepadButtonId::Start: return s.start;
    case GamepadButtonId::Select: return s.select;
    case GamepadButtonId::DPadUp: return s.dpadUp;
    case GamepadButtonId::DPadDown: return s.dpadDown;
    case GamepadButtonId::DPadLeft: return s.dpadLeft;
    case GamepadButtonId::DPadRight: return s.dpadRight;
    case GamepadButtonId::Count: break;
    }
    return false;
}

bool VirtualInput_WasButtonPressed(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    GamepadButtonId id)
{
    return !VirtualInput_IsButtonDown(prev, id) && VirtualInput_IsButtonDown(curr, id);
}

bool VirtualInput_WasButtonReleased(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr,
    GamepadButtonId id)
{
    return VirtualInput_IsButtonDown(prev, id) && !VirtualInput_IsButtonDown(curr, id);
}

bool VirtualInput_IsL2Pressed(const VirtualInputSnapshot& s)
{
    return s.l2Pressed;
}

bool VirtualInput_IsR2Pressed(const VirtualInputSnapshot& s)
{
    return s.r2Pressed;
}

GamepadLeftStickDir VirtualInput_GetLeftDir(const VirtualInputSnapshot& s)
{
    return s.leftDir;
}

GamepadLeftStickDir VirtualInput_GetRightDir(const VirtualInputSnapshot& s)
{
    return s.rightDir;
}

bool VirtualInput_LeftInDeadzone(const VirtualInputSnapshot& s)
{
    return s.leftInDeadzone;
}

bool VirtualInput_RightInDeadzone(const VirtualInputSnapshot& s)
{
    return s.rightInDeadzone;
}

std::int8_t VirtualInputPolicy_ClampNeg1_0_1(int v)
{
    if (v < -1)
    {
        return -1;
    }
    if (v > 1)
    {
        return 1;
    }
    return static_cast<std::int8_t>(v);
}

void VirtualInputPolicy_FillMoveFromDpad(const VirtualInputSnapshot& s, std::int8_t& outX, std::int8_t& outY)
{
    int x = 0;
    int y = 0;
    if (s.dpadLeft)
    {
        x -= 1;
    }
    if (s.dpadRight)
    {
        x += 1;
    }
    if (s.dpadUp)
    {
        y += 1;
    }
    if (s.dpadDown)
    {
        y -= 1;
    }
    outX = VirtualInputPolicy_ClampNeg1_0_1(x);
    outY = VirtualInputPolicy_ClampNeg1_0_1(y);
}

void VirtualInputPolicy_FillMoveFromLeftStick(const VirtualInputSnapshot& s, std::int8_t& outX, std::int8_t& outY)
{
    outX = 0;
    outY = 0;
    switch (s.leftDir)
    {
    case GamepadLeftStickDir::Left: outX = -1; break;
    case GamepadLeftStickDir::Right: outX = 1; break;
    case GamepadLeftStickDir::Up: outY = 1; break;
    case GamepadLeftStickDir::Down: outY = -1; break;
    default: break;
    }
}

VirtualInputPolicyHeld VirtualInputPolicy_MoveHeld(const VirtualInputSnapshot& s)
{
    VirtualInputPolicyHeld h{};
    const bool dpadAny =
        s.dpadUp || s.dpadDown || s.dpadLeft || s.dpadRight;
    if (dpadAny)
    {
        VirtualInputPolicy_FillMoveFromDpad(s, h.moveX, h.moveY);
    }
    else
    {
        VirtualInputPolicy_FillMoveFromLeftStick(s, h.moveX, h.moveY);
    }
    return h;
}

VirtualInputPolicyMenuEdges VirtualInputPolicy_MenuEdges(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    VirtualInputPolicyMenuEdges e{};
    e.confirm = VirtualInput_WasButtonPressed(prev, curr, GamepadButtonId::South);
    e.cancel = VirtualInput_WasButtonPressed(prev, curr, GamepadButtonId::East);
    e.menu = VirtualInput_WasButtonPressed(prev, curr, GamepadButtonId::Start);
    return e;
}

VirtualInputConsumerFrame VirtualInputConsumer_BuildFrame(
    const VirtualInputSnapshot& prev,
    const VirtualInputSnapshot& curr)
{
    VirtualInputConsumerFrame f{};
    const VirtualInputPolicyHeld held = VirtualInputPolicy_MoveHeld(curr);
    f.moveX = held.moveX;
    f.moveY = held.moveY;
    const VirtualInputPolicyMenuEdges e = VirtualInputPolicy_MenuEdges(prev, curr);
    f.confirmPressed = e.confirm;
    f.cancelPressed = e.cancel;
    f.menuPressed = e.menu;
    return f;
}

VirtualInputConsumerFrame VirtualInputConsumer_BuildFrameFromKeyboardState(
    const KeyboardActionState& prevKs,
    const KeyboardActionState& currKs)
{
    VirtualInputConsumerFrame f{};
    std::int8_t mx = 0;
    std::int8_t my = 0;
    if (currKs.left && !currKs.right)
    {
        mx = -1;
    }
    else if (currKs.right && !currKs.left)
    {
        mx = 1;
    }
    if (currKs.up && !currKs.down)
    {
        my = 1;
    }
    else if (currKs.down && !currKs.up)
    {
        my = -1;
    }
    f.moveX = mx;
    f.moveY = my;
    f.menuPressed = !prevKs.tab && currKs.tab;
    f.confirmPressed = !prevKs.enter && currKs.enter;
    f.cancelPressed = !prevKs.backspace && currKs.backspace;
    return f;
}

VirtualInputConsumerFrame VirtualInputConsumer_MergeKeyboardController(
    const VirtualInputConsumerFrame& kb,
    const VirtualInputConsumerFrame& pad)
{
    VirtualInputConsumerFrame u{};
    u.moveX = (pad.moveX != 0) ? pad.moveX : kb.moveX;
    u.moveY = (pad.moveY != 0) ? pad.moveY : kb.moveY;
    u.confirmPressed = kb.confirmPressed || pad.confirmPressed;
    u.cancelPressed = kb.cancelPressed || pad.cancelPressed;
    u.menuPressed = kb.menuPressed || pad.menuPressed;
    return u;
}
