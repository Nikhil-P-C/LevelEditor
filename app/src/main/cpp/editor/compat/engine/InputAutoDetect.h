//
// FALLBACK for engine/InputAutoDetect.h.
// Engine.cpp includes engine/InputAutoDetect.h, but that file was not part of the zip. The editor build puts this
// folder AFTER the project root in the include path, so if you have your own engine/InputAutoDetect.h it is
// used instead and this file is ignored.
//
#pragma once
#include <SDL3/SDL.h>
#include "utils/GameData.h"

namespace InputAutoDetect {

enum class Device { NONE, TOUCH, KEYBOARD_MOUSE, GAMEPAD };

inline ControlType guessInitial(bool allowGamepad = true) {
    if (allowGamepad && SDL_HasGamepad()) return ControlType::GAMEPAD;
#if defined(__ANDROID__)
    return ControlType::BUTTONS;
#else
    return ControlType::KEYBOARD;
#endif
}

inline Device detect(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_FINGER_DOWN:
            return Device::TOUCH;
        case SDL_EVENT_KEY_DOWN:
            return Device::KEYBOARD_MOUSE;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            return e.button.which == SDL_TOUCH_MOUSEID ? Device::NONE : Device::KEYBOARD_MOUSE;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            return Device::GAMEPAD;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            return (e.gaxis.value > 12000 || e.gaxis.value < -12000) ? Device::GAMEPAD : Device::NONE;
        default:
            return Device::NONE;
    }
}

inline ControlType controlFor(Device d, ControlType current) {
    switch (d) {
        case Device::TOUCH:
            return (current == ControlType::BUTTONS || current == ControlType::SEP_JUMP_W_JOYSTICK) ? current : ControlType::BUTTONS;
        case Device::KEYBOARD_MOUSE: return ControlType::KEYBOARD;
        case Device::GAMEPAD:        return ControlType::GAMEPAD;
        default:                     return current;
    }
}

} // namespace InputAutoDetect
