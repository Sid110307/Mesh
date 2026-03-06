#pragma once

#include <core/utils.h>

namespace Keyboard
{
    enum class Key : uint16_t
    {
        NONE = 0,
        ESC, BACKSPACE, TAB, ENTER, SPACE,
        A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        DIGIT1, DIGIT2, DIGIT3, DIGIT4, DIGIT5, DIGIT6, DIGIT7, DIGIT8, DIGIT9, DIGIT0,
        MINUS, EQUAL, LBRACKET, RBRACKET, BACKSLASH,
        SEMICOLON, APOSTROPHE, GRAVE,
        COMMA, DOT, SLASH,
        LSHIFT, RSHIFT, LCTRL, RCTRL, LALT, RALT,
        CAPSLOCK, NUMLOCK, SCROLLLOCK,
        UP, DOWN, LEFT, RIGHT,
        HOME, END, PAGEUP, PAGEDOWN,
        INSERT, DELETE,
        KP_0, KP_1, KP_2, KP_3, KP_4, KP_5, KP_6, KP_7, KP_8, KP_9,
        KP_DOT, KP_SLASH, KP_ASTERISK, KP_MINUS, KP_PLUS, KP_ENTER,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        PRINT_SCREEN, PAUSE,
    };

    struct Modifiers
    {
        bool shift : 1;
        bool ctrl : 1;
        bool alt : 1;
        bool altGr : 1;
        bool capsLock : 1;
        bool numLock : 1;
        bool scrollLock : 1;
    };

    struct Event
    {
        Key key = Key::NONE;
        uint16_t scancode = 0;
        bool e0 = false, e1 = false, pressed = false;
        Modifiers modifiers = {};
        char ascii = 0;
    };

    void init();
    void irq();
    void service();

    bool readEvent(Event& event);
    char readChar();
    Modifiers getModifiers();
    void clear();
}
