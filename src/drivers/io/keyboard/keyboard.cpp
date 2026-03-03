#include <drivers/io/keyboard/keyboard.h>
#include <drivers/io/serial/serial.h>

bool Keyboard::initialized = false;
Spinlock Keyboard::lock;
Keyboard::Queue Keyboard::eventQueue = {};
Keyboard::Modifiers Keyboard::currentModifiers = {false, false, false, false, false, false, false};
Keyboard::DecodeState Keyboard::decodeState = {};

void Keyboard::init()
{
    LockGuardIRQ guard(lock);
    if (initialized) return;

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

    initialized = true;
}

void Keyboard::irq() { while (consumeByte()); }

bool Keyboard::readEvent(Event& event)
{
    LockGuardIRQ guard(lock);
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
    LockGuardIRQ guard(lock);
    return currentModifiers;
}

void Keyboard::clear()
{
    LockGuardIRQ guard(lock);
    eventQueue = Queue{};
    decodeState = {};
}

bool Keyboard::consumeByte()
{
    if (!(inb(STATUS_PORT) & STATUS_OUTPUT_BUFFER)) return false;

    const uint8_t scancode = readData();
    LockGuardIRQ guard(lock);
    Event e = {};
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

bool Keyboard::waitInputClear(int timeout)
{
    while (timeout-- > 0)
    {
        if (!(inb(STATUS_PORT) & STATUS_INPUT_BUFFER)) return true;
        asm volatile ("pause");
    }
    return false;
}

bool Keyboard::waitOutputSet(int timeout)
{
    while (timeout-- > 0)
    {
        if (inb(STATUS_PORT) & STATUS_OUTPUT_BUFFER) return true;
        asm volatile ("pause");
    }
    return false;
}

void Keyboard::writeCommand(const uint8_t cmd)
{
    if (!waitInputClear()) return;
    outb(STATUS_PORT, cmd);
}

void Keyboard::writeData(const uint8_t data)
{
    if (!waitInputClear()) return;
    outb(DATA_PORT, data);
}

uint8_t Keyboard::readData() { return inb(DATA_PORT); }

void Keyboard::setLeds(const bool capsLock, const bool numLock, const bool scrollLock)
{
    uint8_t ledState = 0;
    if (capsLock) ledState |= 1 << 2;
    if (numLock) ledState |= 1 << 1;
    if (scrollLock) ledState |= 1 << 0;

    if (!sendKeyboardCommand(0xED, true, ledState))
        Serial::printf("Keyboard: Failed to set LEDs (CapsLock: %d, NumLock: %d, ScrollLock: %d)\n", capsLock, numLock,
                       scrollLock);
}

bool Keyboard::sendKeyboardCommand(const uint8_t cmd, const bool hasData, const uint8_t data)
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

void Keyboard::decodeByte(const uint8_t scancode, Event& outEvent, bool& produced)
{
    produced = false;
    if (decodeState.prefixE1)
    {
        if (decodeState.pauseLength < sizeof(decodeState.pauseBuffer))
            decodeState.pauseBuffer[decodeState.pauseLength++] = scancode;

        static const uint8_t pauseSet1[] = {0x1D, 0x45, 0xE1, 0x9D, 0xC5};
        if (decodeState.pauseLength == 5)
        {
            bool match = true;
            for (int i = 0; i < 5; ++i) if (decodeState.pauseBuffer[i] != pauseSet1[i]) match = false;

            decodeState.prefixE1 = false;
            decodeState.pauseLength = 0;

            if (match)
            {
                outEvent = {};
                outEvent.e1 = true;
                outEvent.pressed = true;
                outEvent.scancode = 0;
                outEvent.key = Key::PAUSE;
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
                outEvent.key = Key::PRINT_SCREEN;
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

Keyboard::Key Keyboard::mapKey(const uint8_t makeCode, const bool prefixE0)
{
    if (prefixE0)
        switch (makeCode)
        {
            case 0x48: return Key::UP;
            case 0x50: return Key::DOWN;
            case 0x4B: return Key::LEFT;
            case 0x4D: return Key::RIGHT;
            case 0x47: return Key::HOME;
            case 0x4F: return Key::END;
            case 0x49: return Key::PAGEUP;
            case 0x51: return Key::PAGEDOWN;
            case 0x52: return Key::INSERT;
            case 0x53: return Key::DELETE;
            case 0x1D: return Key::RCTRL;
            case 0x38: return Key::RALT;
            case 0x1C: return Key::KP_ENTER;
            case 0x35: return Key::KP_SLASH;
            default: return Key::NONE;
        }

    switch (makeCode)
    {
        case 0x01: return Key::ESC;
        case 0x0E: return Key::BACKSPACE;
        case 0x0F: return Key::TAB;
        case 0x1C: return Key::ENTER;
        case 0x39: return Key::SPACE;
        case 0x2A: return Key::LSHIFT;
        case 0x36: return Key::RSHIFT;
        case 0x1D: return Key::LCTRL;
        case 0x38: return Key::LALT;
        case 0x3A: return Key::CAPSLOCK;
        case 0x45: return Key::NUMLOCK;
        case 0x46: return Key::SCROLLLOCK;
        case 0x3B: return Key::F1;
        case 0x3C: return Key::F2;
        case 0x3D: return Key::F3;
        case 0x3E: return Key::F4;
        case 0x3F: return Key::F5;
        case 0x40: return Key::F6;
        case 0x41: return Key::F7;
        case 0x42: return Key::F8;
        case 0x43: return Key::F9;
        case 0x44: return Key::F10;
        case 0x57: return Key::F11;
        case 0x58: return Key::F12;
        case 0x02: return Key::DIGIT1;
        case 0x03: return Key::DIGIT2;
        case 0x04: return Key::DIGIT3;
        case 0x05: return Key::DIGIT4;
        case 0x06: return Key::DIGIT5;
        case 0x07: return Key::DIGIT6;
        case 0x08: return Key::DIGIT7;
        case 0x09: return Key::DIGIT8;
        case 0x0A: return Key::DIGIT9;
        case 0x0B: return Key::DIGIT0;
        case 0x0C: return Key::MINUS;
        case 0x0D: return Key::EQUAL;
        case 0x10: return Key::Q;
        case 0x11: return Key::W;
        case 0x12: return Key::E;
        case 0x13: return Key::R;
        case 0x14: return Key::T;
        case 0x15: return Key::Y;
        case 0x16: return Key::U;
        case 0x17: return Key::I;
        case 0x18: return Key::O;
        case 0x19: return Key::P;
        case 0x1A: return Key::LBRACKET;
        case 0x1B: return Key::RBRACKET;
        case 0x2B: return Key::BACKSLASH;
        case 0x1E: return Key::A;
        case 0x1F: return Key::S;
        case 0x20: return Key::D;
        case 0x21: return Key::F;
        case 0x22: return Key::G;
        case 0x23: return Key::H;
        case 0x24: return Key::J;
        case 0x25: return Key::K;
        case 0x26: return Key::L;
        case 0x27: return Key::SEMICOLON;
        case 0x28: return Key::APOSTROPHE;
        case 0x29: return Key::GRAVE;
        case 0x2C: return Key::Z;
        case 0x2D: return Key::X;
        case 0x2E: return Key::C;
        case 0x2F: return Key::V;
        case 0x30: return Key::B;
        case 0x31: return Key::N;
        case 0x32: return Key::M;
        case 0x33: return Key::COMMA;
        case 0x34: return Key::DOT;
        case 0x35: return Key::SLASH;
        case 0x52: return Key::KP_0;
        case 0x4F: return Key::KP_1;
        case 0x50: return Key::KP_2;
        case 0x51: return Key::KP_3;
        case 0x4B: return Key::KP_4;
        case 0x4C: return Key::KP_5;
        case 0x4D: return Key::KP_6;
        case 0x47: return Key::KP_7;
        case 0x48: return Key::KP_8;
        case 0x49: return Key::KP_9;
        case 0x53: return Key::KP_DOT;
        case 0x37: return Key::KP_ASTERISK;
        case 0x4A: return Key::KP_MINUS;
        case 0x4E: return Key::KP_PLUS;
        default: return Key::NONE;
    }
}

char Keyboard::mapAscii(Key key, const Modifiers& modifiers)
{
    if (key == Key::ENTER) return '\n';
    if (key == Key::TAB) return '\t';
    if (key == Key::BACKSPACE) return '\b';
    if (key == Key::SPACE) return ' ';

    if (key >= Key::A && key <= Key::Z)
    {
        const char c = static_cast<char>('a' + (static_cast<int>(key) - static_cast<int>(Key::A)));
        return modifiers.shift ^ modifiers.capsLock ? static_cast<char>(c - 'a' + 'A') : c;
    }

    struct Pair
    {
        Key k;
        char n, s;
    };

    static constexpr Pair pairs[] = {
        {Key::DIGIT1, '1', '!'},
        {Key::DIGIT2, '2', '@'},
        {Key::DIGIT3, '3', '#'},
        {Key::DIGIT4, '4', '$'},
        {Key::DIGIT5, '5', '%'},
        {Key::DIGIT6, '6', '^'},
        {Key::DIGIT7, '7', '&'},
        {Key::DIGIT8, '8', '*'},
        {Key::DIGIT9, '9', '('},
        {Key::DIGIT0, '0', ')'},
        {Key::MINUS, '-', '_'},
        {Key::EQUAL, '=', '+'},
        {Key::LBRACKET, '[', '{'},
        {Key::RBRACKET, ']', '}'},
        {Key::BACKSLASH, '\\', '|'},
        {Key::SEMICOLON, ';', ':'},
        {Key::APOSTROPHE, '\'', '"'},
        {Key::GRAVE, '`', '~'},
        {Key::COMMA, ',', '<'},
        {Key::DOT, '.', '>'},
        {Key::SLASH, '/', '?'},
    };
    for (const auto& [k, n, s] : pairs) if (k == key) return modifiers.shift ? s : n;

    if (key >= Key::KP_0 && key <= Key::KP_9)
        return !modifiers.numLock ? 0 : static_cast<char>('0' + (static_cast<int>(key) - static_cast<int>(Key::KP_0)));
    if (key == Key::KP_DOT) return modifiers.numLock ? '.' : 0;
    if (key == Key::KP_PLUS) return '+';
    if (key == Key::KP_MINUS) return '-';
    if (key == Key::KP_ASTERISK) return '*';
    if (key == Key::KP_SLASH) return '/';
    if (key == Key::KP_ENTER) return '\n';

    return 0;
}

void Keyboard::applyModifiers(const Event& event)
{
    if (event.key == Key::LSHIFT || event.key == Key::RSHIFT) currentModifiers.shift = event.pressed;
    else if (event.key == Key::LCTRL || event.key == Key::RCTRL) currentModifiers.ctrl = event.pressed;
    else if (event.key == Key::LALT) currentModifiers.alt = event.pressed;
    else if (event.key == Key::RALT) currentModifiers.altGr = event.pressed;

    if (event.pressed && event.key == Key::CAPSLOCK)
    {
        currentModifiers.capsLock = !currentModifiers.capsLock;
        setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
    }
    if (event.pressed && event.key == Key::NUMLOCK)
    {
        currentModifiers.numLock = !currentModifiers.numLock;
        setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
    }
    if (event.pressed && event.key == Key::SCROLLLOCK)
    {
        currentModifiers.scrollLock = !currentModifiers.scrollLock;
        setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
    }
}

void Keyboard::pushEvent(const Event& event)
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
