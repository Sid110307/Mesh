#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <memory/spinlock.h>

constexpr uint16_t DATA_PORT = 0x60, STATUS_PORT = 0x64;
constexpr uint8_t STATUS_OUTPUT_BUFFER = 1 << 0, STATUS_INPUT_BUFFER = 1 << 1;
constexpr size_t QUEUE_SIZE = 256;

struct Queue
{
    Keyboard::Event events[QUEUE_SIZE] = {};
    size_t head = 0, tail = 0, count = 0;
};

struct DecodeState
{
    bool prefixE0 = false, prefixE1 = false;
    uint8_t pauseBuffer[8] = {}, pauseLength = 0, printScreenBuffer[4] = {}, printScreenLength = 0;
};

struct Pair
{
    Keyboard::Key k;
    char n, s;
};

constexpr Pair pairs[] = {
    {Keyboard::Key::DIGIT1, '1', '!'},
    {Keyboard::Key::DIGIT2, '2', '@'},
    {Keyboard::Key::DIGIT3, '3', '#'},
    {Keyboard::Key::DIGIT4, '4', '$'},
    {Keyboard::Key::DIGIT5, '5', '%'},
    {Keyboard::Key::DIGIT6, '6', '^'},
    {Keyboard::Key::DIGIT7, '7', '&'},
    {Keyboard::Key::DIGIT8, '8', '*'},
    {Keyboard::Key::DIGIT9, '9', '('},
    {Keyboard::Key::DIGIT0, '0', ')'},
    {Keyboard::Key::MINUS, '-', '_'},
    {Keyboard::Key::EQUAL, '=', '+'},
    {Keyboard::Key::LBRACKET, '[', '{'},
    {Keyboard::Key::RBRACKET, ']', '}'},
    {Keyboard::Key::BACKSLASH, '\\', '|'},
    {Keyboard::Key::SEMICOLON, ';', ':'},
    {Keyboard::Key::APOSTROPHE, '\'', '"'},
    {Keyboard::Key::GRAVE, '`', '~'},
    {Keyboard::Key::COMMA, ',', '<'},
    {Keyboard::Key::DOT, '.', '>'},
    {Keyboard::Key::SLASH, '/', '?'},
};

bool keyboardInitialized = false, ledsDirty = false;
Spinlock lock;
Queue eventQueue = {};
Keyboard::Modifiers currentModifiers = {false, false, false, false, false, false, false};
DecodeState decodeState = {};

bool waitInputClear(int timeout = 100000)
{
    while (timeout-- > 0)
    {
        if (!(inb(STATUS_PORT) & STATUS_INPUT_BUFFER)) return true;
        asm volatile ("pause");
    }
    return false;
}

bool waitOutputSet(int timeout = 100000)
{
    while (timeout-- > 0)
    {
        if (inb(STATUS_PORT) & STATUS_OUTPUT_BUFFER) return true;
        asm volatile ("pause");
    }
    return false;
}

void writeCommand(const uint8_t cmd)
{
    if (!waitInputClear()) return;
    outb(STATUS_PORT, cmd);
}

void writeData(const uint8_t data)
{
    if (!waitInputClear()) return;
    outb(DATA_PORT, data);
}

uint8_t readData() { return inb(DATA_PORT); }

bool sendKeyboardCommand(const uint8_t cmd, const bool hasData, const uint8_t data = 0)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        writeData(cmd);
        if (!waitOutputSet()) continue;

        uint8_t resp = readData();
        if (resp == 0xFE) continue;
        if (resp != 0xFA)
        {
            Serial::printf("Keyboard: Unexpected response 0x%x after sending 0x%x\n", resp, cmd);
            return false;
        }
        if (!hasData) return true;

        writeData(data);
        if (!waitOutputSet())
        {
            Serial::printf("Keyboard: Timeout waiting for output buffer to be set after sending data 0x%x for 0x%x\n",
                           data, cmd);
            continue;
        }

        resp = readData();
        if (resp == 0xFE) continue;
        if (resp == 0xFA) return true;

        Serial::printf("Keyboard: Unexpected response 0x%x after sending data 0x%x for 0x%x\n", resp, data, cmd);
        return false;
    }

    Serial::printf("Keyboard: Failed to send 0x%x after multiple attempts\n", cmd);
    return false;
}

void setLeds(const bool capsLock, const bool numLock, const bool scrollLock)
{
    uint8_t ledState = 0;
    if (capsLock) ledState |= 1 << 2;
    if (numLock) ledState |= 1 << 1;
    if (scrollLock) ledState |= 1 << 0;

    if (!sendKeyboardCommand(0xED, true, ledState))
        Serial::printf("Keyboard: Failed to set LEDs (CapsLock: %d, NumLock: %d, ScrollLock: %d)\n", capsLock, numLock,
                       scrollLock);
}

Keyboard::Key mapKey(const uint8_t makeCode, const bool prefixE0)
{
    if (prefixE0)
        switch (makeCode)
        {
            case 0x48: return Keyboard::Key::UP;
            case 0x50: return Keyboard::Key::DOWN;
            case 0x4B: return Keyboard::Key::LEFT;
            case 0x4D: return Keyboard::Key::RIGHT;
            case 0x47: return Keyboard::Key::HOME;
            case 0x4F: return Keyboard::Key::END;
            case 0x49: return Keyboard::Key::PAGEUP;
            case 0x51: return Keyboard::Key::PAGEDOWN;
            case 0x52: return Keyboard::Key::INSERT;
            case 0x53: return Keyboard::Key::DELETE;
            case 0x1D: return Keyboard::Key::RCTRL;
            case 0x38: return Keyboard::Key::RALT;
            case 0x1C: return Keyboard::Key::KP_ENTER;
            case 0x35: return Keyboard::Key::KP_SLASH;
            default: return Keyboard::Key::NONE;
        }

    switch (makeCode)
    {
        case 0x01: return Keyboard::Key::ESC;
        case 0x0E: return Keyboard::Key::BACKSPACE;
        case 0x0F: return Keyboard::Key::TAB;
        case 0x1C: return Keyboard::Key::ENTER;
        case 0x39: return Keyboard::Key::SPACE;
        case 0x2A: return Keyboard::Key::LSHIFT;
        case 0x36: return Keyboard::Key::RSHIFT;
        case 0x1D: return Keyboard::Key::LCTRL;
        case 0x38: return Keyboard::Key::LALT;
        case 0x3A: return Keyboard::Key::CAPSLOCK;
        case 0x45: return Keyboard::Key::NUMLOCK;
        case 0x46: return Keyboard::Key::SCROLLLOCK;
        case 0x3B: return Keyboard::Key::F1;
        case 0x3C: return Keyboard::Key::F2;
        case 0x3D: return Keyboard::Key::F3;
        case 0x3E: return Keyboard::Key::F4;
        case 0x3F: return Keyboard::Key::F5;
        case 0x40: return Keyboard::Key::F6;
        case 0x41: return Keyboard::Key::F7;
        case 0x42: return Keyboard::Key::F8;
        case 0x43: return Keyboard::Key::F9;
        case 0x44: return Keyboard::Key::F10;
        case 0x57: return Keyboard::Key::F11;
        case 0x58: return Keyboard::Key::F12;
        case 0x02: return Keyboard::Key::DIGIT1;
        case 0x03: return Keyboard::Key::DIGIT2;
        case 0x04: return Keyboard::Key::DIGIT3;
        case 0x05: return Keyboard::Key::DIGIT4;
        case 0x06: return Keyboard::Key::DIGIT5;
        case 0x07: return Keyboard::Key::DIGIT6;
        case 0x08: return Keyboard::Key::DIGIT7;
        case 0x09: return Keyboard::Key::DIGIT8;
        case 0x0A: return Keyboard::Key::DIGIT9;
        case 0x0B: return Keyboard::Key::DIGIT0;
        case 0x0C: return Keyboard::Key::MINUS;
        case 0x0D: return Keyboard::Key::EQUAL;
        case 0x10: return Keyboard::Key::Q;
        case 0x11: return Keyboard::Key::W;
        case 0x12: return Keyboard::Key::E;
        case 0x13: return Keyboard::Key::R;
        case 0x14: return Keyboard::Key::T;
        case 0x15: return Keyboard::Key::Y;
        case 0x16: return Keyboard::Key::U;
        case 0x17: return Keyboard::Key::I;
        case 0x18: return Keyboard::Key::O;
        case 0x19: return Keyboard::Key::P;
        case 0x1A: return Keyboard::Key::LBRACKET;
        case 0x1B: return Keyboard::Key::RBRACKET;
        case 0x2B: return Keyboard::Key::BACKSLASH;
        case 0x1E: return Keyboard::Key::A;
        case 0x1F: return Keyboard::Key::S;
        case 0x20: return Keyboard::Key::D;
        case 0x21: return Keyboard::Key::F;
        case 0x22: return Keyboard::Key::G;
        case 0x23: return Keyboard::Key::H;
        case 0x24: return Keyboard::Key::J;
        case 0x25: return Keyboard::Key::K;
        case 0x26: return Keyboard::Key::L;
        case 0x27: return Keyboard::Key::SEMICOLON;
        case 0x28: return Keyboard::Key::APOSTROPHE;
        case 0x29: return Keyboard::Key::GRAVE;
        case 0x2C: return Keyboard::Key::Z;
        case 0x2D: return Keyboard::Key::X;
        case 0x2E: return Keyboard::Key::C;
        case 0x2F: return Keyboard::Key::V;
        case 0x30: return Keyboard::Key::B;
        case 0x31: return Keyboard::Key::N;
        case 0x32: return Keyboard::Key::M;
        case 0x33: return Keyboard::Key::COMMA;
        case 0x34: return Keyboard::Key::DOT;
        case 0x35: return Keyboard::Key::SLASH;
        case 0x52: return Keyboard::Key::KP_0;
        case 0x4F: return Keyboard::Key::KP_1;
        case 0x50: return Keyboard::Key::KP_2;
        case 0x51: return Keyboard::Key::KP_3;
        case 0x4B: return Keyboard::Key::KP_4;
        case 0x4C: return Keyboard::Key::KP_5;
        case 0x4D: return Keyboard::Key::KP_6;
        case 0x47: return Keyboard::Key::KP_7;
        case 0x48: return Keyboard::Key::KP_8;
        case 0x49: return Keyboard::Key::KP_9;
        case 0x53: return Keyboard::Key::KP_DOT;
        case 0x37: return Keyboard::Key::KP_ASTERISK;
        case 0x4A: return Keyboard::Key::KP_MINUS;
        case 0x4E: return Keyboard::Key::KP_PLUS;
        default: return Keyboard::Key::NONE;
    }
}

char mapAscii(Keyboard::Key key, const Keyboard::Modifiers& modifiers)
{
    if (key == Keyboard::Key::ENTER) return '\n';
    if (key == Keyboard::Key::TAB) return '\t';
    if (key == Keyboard::Key::BACKSPACE) return '\b';
    if (key == Keyboard::Key::SPACE) return ' ';

    if (key >= Keyboard::Key::A && key <= Keyboard::Key::Z)
    {
        const char c = static_cast<char>('a' + (static_cast<int>(key) - static_cast<int>(Keyboard::Key::A)));
        return modifiers.shift ^ modifiers.capsLock ? static_cast<char>(c - 'a' + 'A') : c;
    }
    for (const auto& [k, n, s] : pairs) if (k == key) return modifiers.shift ? s : n;

    if (key >= Keyboard::Key::KP_0 && key <= Keyboard::Key::KP_9)
        return !modifiers.numLock
                   ? 0
                   : static_cast<char>('0' + (static_cast<int>(key) - static_cast<int>(Keyboard::Key::KP_0)));
    if (key == Keyboard::Key::KP_DOT) return modifiers.numLock ? '.' : 0;
    if (key == Keyboard::Key::KP_PLUS) return '+';
    if (key == Keyboard::Key::KP_MINUS) return '-';
    if (key == Keyboard::Key::KP_ASTERISK) return '*';
    if (key == Keyboard::Key::KP_SLASH) return '/';
    if (key == Keyboard::Key::KP_ENTER) return '\n';

    return 0;
}

void decodeByte(const uint8_t scancode, Keyboard::Event& outEvent, bool& produced)
{
    produced = false;
    if (decodeState.prefixE1)
    {
        if (decodeState.pauseLength < sizeof(decodeState.pauseBuffer))
            decodeState.pauseBuffer[decodeState.pauseLength++] = scancode;

        if (decodeState.pauseLength == 5)
        {
            bool match = true;
            for (int i = 0; i < 5; ++i)
                if (const uint8_t set[] = {0x1D, 0x45, 0xE1, 0x9D, 0xC5}; decodeState.pauseBuffer[i] != set[i])
                    match = false;

            decodeState.prefixE1 = false;
            decodeState.pauseLength = 0;

            if (match)
            {
                outEvent = {};
                outEvent.e1 = true;
                outEvent.pressed = true;
                outEvent.scancode = 0;
                outEvent.key = Keyboard::Key::PAUSE;
                produced = true;
            }
            return;
        }

        if (decodeState.pauseLength >= sizeof(decodeState.pauseBuffer))
        {
            decodeState.prefixE1 = false;
            decodeState.pauseLength = 0;
        }
        return;
    }

    if (scancode == 0xE0)
    {
        decodeState.prefixE0 = true;
        return;
    }
    if (scancode == 0xE1)
    {
        decodeState.prefixE1 = true;
        decodeState.pauseLength = 0;

        return;
    }

    if (decodeState.prefixE0)
    {
        if (decodeState.printScreenLength < sizeof(decodeState.printScreenBuffer))
            decodeState.printScreenBuffer[decodeState.printScreenLength++] = scancode;

        if (decodeState.printScreenLength == 1)
        {
            if (scancode != 0x2A && scancode != 0xB7)
            {
                outEvent = {};
                outEvent.e0 = true;
                outEvent.e1 = false;
                outEvent.pressed = (scancode & 0x80) == 0;
                outEvent.scancode = scancode & 0x7F;
                outEvent.key = mapKey(outEvent.scancode, true);

                produced = true;
                decodeState.prefixE0 = false;
                decodeState.printScreenLength = 0;

                return;
            }
            return;
        }

        if (decodeState.printScreenLength == 2)
        {
            if (scancode != 0xE0)
            {
                decodeState.prefixE0 = false;
                decodeState.printScreenLength = 0;

                return;
            }
            return;
        }

        if (decodeState.printScreenLength == 3)
        {
            decodeState.prefixE0 = false;
            decodeState.printScreenLength = 0;

            if ((decodeState.printScreenBuffer[0] == 0x2A && decodeState.printScreenBuffer[2] == 0x37) ||
                (decodeState.printScreenBuffer[0] == 0xB7 && decodeState.printScreenBuffer[2] == 0xAA))
            {
                outEvent = {};
                outEvent.e0 = true;
                outEvent.pressed = decodeState.printScreenBuffer[0] == 0x2A && decodeState.printScreenBuffer[2] == 0x37;
                outEvent.key = Keyboard::Key::PRINT_SCREEN;
                outEvent.scancode = 0;
                produced = true;
            }
            return;
        }

        decodeState.prefixE0 = false;
        decodeState.printScreenLength = 0;

        return;
    }

    outEvent = {};
    outEvent.e0 = false;
    outEvent.e1 = false;
    outEvent.pressed = (scancode & 0x80) == 0;
    outEvent.scancode = scancode & 0x7F;
    outEvent.key = mapKey(scancode & 0x7F, false);
    produced = true;
}

void applyModifiers(const Keyboard::Event& event)
{
    if (event.key == Keyboard::Key::LSHIFT || event.key == Keyboard::Key::RSHIFT)
        currentModifiers.shift = event.pressed;
    else if (event.key == Keyboard::Key::LCTRL || event.key == Keyboard::Key::RCTRL)
        currentModifiers.ctrl = event.pressed;
    else if (event.key == Keyboard::Key::LALT) currentModifiers.alt = event.pressed;
    else if (event.key == Keyboard::Key::RALT) currentModifiers.altGr = event.pressed;

    if (event.pressed && event.key == Keyboard::Key::CAPSLOCK)
    {
        currentModifiers.capsLock = !currentModifiers.capsLock;
        ledsDirty = true;
    }
    if (event.pressed && event.key == Keyboard::Key::NUMLOCK)
    {
        currentModifiers.numLock = !currentModifiers.numLock;
        ledsDirty = true;
    }
    if (event.pressed && event.key == Keyboard::Key::SCROLLLOCK)
    {
        currentModifiers.scrollLock = !currentModifiers.scrollLock;
        ledsDirty = true;
    }
}

void pushEvent(const Keyboard::Event& event)
{
    if (eventQueue.count == QUEUE_SIZE)
    {
        eventQueue.head = (eventQueue.head + 1) % QUEUE_SIZE;
        eventQueue.count--;
    }

    eventQueue.events[eventQueue.tail] = event;
    eventQueue.tail = (eventQueue.tail + 1) % QUEUE_SIZE;
    eventQueue.count++;
}

bool consumeByte()
{
    if (!(inb(STATUS_PORT) & STATUS_OUTPUT_BUFFER)) return false;
    LockGuard guard(lock);

    const uint8_t scancode = readData();
    Keyboard::Event e = {};
    bool produced = false;

    decodeByte(scancode, e, produced);
    if (produced)
    {
        applyModifiers(e);
        e.modifiers = currentModifiers;
        e.ascii = mapAscii(e.key, currentModifiers);

        pushEvent(e);
    }

    return true;
}

void Keyboard::init()
{
    LockGuard guard(lock);
    if (keyboardInitialized) return;

    writeCommand(0xAD);
    writeCommand(0xA7);
    while (inb(STATUS_PORT) & STATUS_OUTPUT_BUFFER) readData();
    writeCommand(0x20);

    uint8_t config = 0;
    if (waitOutputSet()) config = readData();
    config |= 1 << 0;
    config &= ~(1 << 1);

    writeCommand(0x60);
    writeData(config);
    writeCommand(0xAE);

    currentModifiers = {false, false, false, false, false, false, false};
    sendKeyboardCommand(0xF5, false);
    setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
    sendKeyboardCommand(0xF4, false);

    keyboardInitialized = true;
}

void Keyboard::irq() { while (consumeByte()); }

void Keyboard::service()
{
    LockGuard guard(lock);
    if (!keyboardInitialized || !ledsDirty) return;

    ledsDirty = false;
    setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
}

bool Keyboard::readEvent(Event& event)
{
    LockGuard guard(lock);
    if (eventQueue.count == 0) return false;

    event = eventQueue.events[eventQueue.head];
    eventQueue.head = (eventQueue.head + 1) % QUEUE_SIZE;
    eventQueue.count--;

    return true;
}

char Keyboard::readChar()
{
    Event event = {};
    if (!readEvent(event)) return 0;

    return event.pressed && event.ascii ? event.ascii : 0;
}

Keyboard::Modifiers Keyboard::getModifiers()
{
    LockGuard guard(lock);
    return currentModifiers;
}

void Keyboard::clear()
{
    LockGuard guard(lock);
    eventQueue = Queue{};
    decodeState = {};
}
