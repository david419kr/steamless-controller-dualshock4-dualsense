#pragma once
#include <cstdint>

enum class BackButtonId : uint8_t {
    L4 = 0,
    L5 = 1,
    R4 = 2,
    R5 = 3,
    Count = 4,
};

enum class BackButtonAction : uint8_t {
    None = 0,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    South,
    East,
    West,
    North,
    LeftBumper,
    RightBumper,
    LeftTrigger,
    RightTrigger,
    LeftStick,
    RightStick,
    Back,
    Start,
    Guide,
    GL,
    GR,
};

struct BackButtonMappings {
    BackButtonAction l4 = BackButtonAction::None;
    BackButtonAction l5 = BackButtonAction::None;
    BackButtonAction r4 = BackButtonAction::None;
    BackButtonAction r5 = BackButtonAction::None;

    BackButtonAction Get(BackButtonId id) const {
        switch (id) {
        case BackButtonId::L4: return l4;
        case BackButtonId::L5: return l5;
        case BackButtonId::R4: return r4;
        case BackButtonId::R5: return r5;
        default: return BackButtonAction::None;
        }
    }

    void Set(BackButtonId id, BackButtonAction action) {
        switch (id) {
        case BackButtonId::L4: l4 = action; break;
        case BackButtonId::L5: l5 = action; break;
        case BackButtonId::R4: r4 = action; break;
        case BackButtonId::R5: r5 = action; break;
        default: break;
        }
    }

    bool AnyAssigned() const {
        return l4 != BackButtonAction::None ||
               l5 != BackButtonAction::None ||
               r4 != BackButtonAction::None ||
               r5 != BackButtonAction::None;
    }
};
