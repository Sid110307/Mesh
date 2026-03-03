#pragma once

#include <kernel/sync/spinlock.h>
#include <kernel/core/utils.h>

class Keyboard
{
public:
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

    static void init();
    static void irq();

    static bool readEvent(Event& event);
    static char readChar();
    static Modifiers getModifiers();
    static void clear();

    static constexpr uint16_t DATA_PORT = 0x60, STATUS_PORT = 0x64;
    static constexpr uint8_t STATUS_OUTPUT_BUFFER = 1 << 0, STATUS_INPUT_BUFFER = 1 << 1;
    static constexpr size_t QUEUE_SIZE = 256;

private:
    struct Queue
    {
        Event events[QUEUE_SIZE] = {};
        size_t head = 0, tail = 0, count = 0;
    };

    struct DecodeState
    {
        bool prefixE0 = false, prefixE1 = false;
        uint8_t pauseBuffer[8] = {}, pauseLength = 0, printScreenBuffer[4] = {}, printScreenLength = 0;
    };

    static bool initialized;
    static Spinlock lock;
    static Queue eventQueue;
    static Modifiers currentModifiers;
    static DecodeState decodeState;

    static bool consumeByte();
    static bool waitInputClear(int timeout = 100000);
    static bool waitOutputSet(int timeout = 100000);

    static void writeCommand(uint8_t cmd);
    static void writeData(uint8_t data);
    static uint8_t readData();

    static void setLeds(bool capsLock, bool numLock, bool scrollLock);
    static bool sendKeyboardCommand(uint8_t cmd, bool hasData, uint8_t data = 0);

    static void decodeByte(uint8_t scancode, Event& outEvent, bool& produced);
    static Key mapKey(uint8_t makeCode, bool prefixE0);
    static char mapAscii(Key key, const Modifiers& modifiers);

    static void applyModifiers(const Event& event);
    static void pushEvent(const Event& event);
};
