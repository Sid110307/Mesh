#pragma once

#include <kernel/sync/spinlock.h>
#include <kernel/core/utils.h>

class Keyboard
{
public:
    enum class Key : uint16_t
    {
        NONE = 0,
        ESC,
        BACKSPACE,
        TAB,
        ENTER,
        SPACE,
        LSHIFT,
        RSHIFT,
        LCTRL,
        RCTRL,
        LALT,
        RALT,
        CAPSLOCK,
        NUMLOCK,
        SCROLLLOCK,
        UP,
        DOWN,
        LEFT,
        RIGHT,
        HOME,
        END,
        PAGEUP,
        PAGEDOWN,
        INSERT,
        DELETE,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        PAUSE,
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
        Key key;
        uint8_t scancode;
        bool extended, pressed;
        Modifiers modifiers;
        char ascii;
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

    static bool initialized, prefixE0, prefixE1;
    static Spinlock lock;
    static Queue eventQueue;
    static Modifiers currentModifiers;

    static bool consumeByte();
    static bool waitInputClear(int timeout = 100000);
    static bool waitOutputSet(int timeout = 100000);

    static void writeCommand(uint8_t cmd);
    static void writeData(uint8_t data);
    static uint8_t readData();

    static void setLeds(bool capsLock, bool numLock, bool scrollLock);
    static bool sendKeyboardCommand(uint8_t cmd, uint8_t data = 0);

    static void handleScancode(uint8_t scancode);
    static Key mapKeys(uint8_t scancode);
    static char mapAscii(uint8_t scancode, const Modifiers& modifiers);

    static void pushEvent(const Event& event);
};
