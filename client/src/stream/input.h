#pragma once

typedef enum : uint8_t {
    CursorLeftDown = 0,
    CursorLeftUp,
    CursorLeftClick,
    CursorRightClick,
    CursorMove,
    CursorScroll,
    GamepadButtonX,
    GamepadButtonY,
    GamepadButtonA,
    GamepadButtonB,
    GamepadButtonL1,
    GamepadButtonR1,
    GamepadButtonL2,
    GamepadButtonR2,
    GamepadUp,
    GamepadDown,
    GamepadLeft,
    GamepadRight,
    GamepadLeftStick,
    GamepadRightStick,
    GamepadButtonStart,
    GamepadButtonSelect,
} InputType;

#pragma pack(push, 1)
typedef struct {
    uint8_t type;
    uint32_t data0;
    uint32_t data1;
} InputCommand;
#pragma pack(pop)
