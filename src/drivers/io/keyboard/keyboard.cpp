#include <drivers/io/keyboard/keyboard.h>
#include <drivers/io/serial/serial.h>

bool Keyboard::initialized = false, Keyboard::prefixE0 = false, Keyboard::prefixE1 = false;
Spinlock Keyboard::lock;
Keyboard::Queue Keyboard::eventQueue;
Keyboard::Modifiers Keyboard::currentModifiers = {false, false, false, false, false, false, false};

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
    prefixE0 = prefixE1 = false;
    eventQueue = {};

    setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
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

    return event.pressed ? event.ascii : 0;
}

Keyboard::Modifiers Keyboard::getModifiers()
{
    LockGuardIRQ guard(lock);
    return currentModifiers;
}

void Keyboard::clear()
{
    LockGuardIRQ guard(lock);
    eventQueue = {};
    prefixE0 = prefixE1 = false;
}

bool Keyboard::consumeByte()
{
    if (!(inb(STATUS_PORT) & STATUS_OUTPUT_BUFFER)) return false;
    const uint8_t scancode = readData();

    if (scancode == 0xE0)
    {
        LockGuardIRQ guard(lock);
        prefixE0 = true;

        return true;
    }
    if (scancode == 0xE1)
    {
        LockGuardIRQ guard(lock);
        prefixE1 = true;

        return true;
    }

    LockGuardIRQ guard(lock);
    handleScancode(scancode);
    prefixE0 = prefixE1 = false;

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
    if (!waitInputClear())
    {
        Serial::printf("Keyboard: Timeout waiting for input buffer to clear when writing 0x%lx\n", cmd);
        return;
    }
    outb(STATUS_PORT, cmd);
}

void Keyboard::writeData(const uint8_t data)
{
    if (!waitInputClear())
    {
        Serial::printf("Keyboard: Timeout waiting for input buffer to clear when writing data 0x%lx\n", data);
        return;
    }
    outb(DATA_PORT, data);
}

uint8_t Keyboard::readData() { return inb(DATA_PORT); }

void Keyboard::setLeds(const bool capsLock, const bool numLock, const bool scrollLock)
{
    uint8_t ledState = 0;
    if (capsLock) ledState |= 1 << 2;
    if (numLock) ledState |= 1 << 1;
    if (scrollLock) ledState |= 1 << 0;

    if (!sendKeyboardCommand(0xED, ledState))
        Serial::printf("Keyboard: Failed to set LEDs (CapsLock: %d, NumLock: %d, ScrollLock: %d)\n", capsLock, numLock,
                       scrollLock);
}

bool Keyboard::sendKeyboardCommand(const uint8_t cmd, const uint8_t data)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        uint8_t resp = 0;
        writeData(cmd);
        if (!waitOutputSet())
        {
            Serial::printf("Keyboard: Timeout waiting for output buffer to be set after sending 0x%lx\n", cmd);
            continue;
        }
        resp = readData();
        if (resp == 0xFE) continue;
        if (resp != 0xFA)
        {
            Serial::printf("Keyboard: Unexpected response 0x%lx after sending 0x%lx\n", resp, cmd);
            return false;
        }

        writeData(data);
        if (!waitOutputSet())
        {
            Serial::printf("Keyboard: Timeout waiting for output buffer to be set after sending data 0x%lx for 0x%lx\n",
                           data, cmd);
            continue;
        }
        resp = readData();
        if (resp == 0xFE) continue;
        if (resp == 0xFA) return true;

        Serial::printf("Keyboard: Unexpected response 0x%lx after sending data 0x%lx for 0x%lx\n", resp, data, cmd);
        return false;
    }

    Serial::printf("Keyboard: Failed to send 0x%lx after multiple attempts\n", cmd);
    return false;
}

void Keyboard::handleScancode(const uint8_t scancode)
{
    const bool breakCode = scancode & 0x80, pressed = !breakCode;

    Event event = {};
    event.scancode = static_cast<uint8_t>(scancode & 0x7F);
    event.extended = prefixE0 || prefixE1;
    event.pressed = pressed;
    event.key = mapKeys(scancode);

    if (event.key == Key::LSHIFT || event.key == Key::RSHIFT) currentModifiers.shift = pressed;
    else if (event.key == Key::LCTRL || event.key == Key::RCTRL) currentModifiers.ctrl = pressed;
    else if (event.key == Key::LALT) currentModifiers.alt = pressed;
    else if (event.key == Key::RALT) currentModifiers.altGr = pressed;

    if (pressed && event.key == Key::CAPSLOCK)
    {
        currentModifiers.capsLock = !currentModifiers.capsLock;
        setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
    }
    if (pressed && event.key == Key::NUMLOCK)
    {
        currentModifiers.numLock = !currentModifiers.numLock;
        setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
    }
    if (pressed && event.key == Key::SCROLLLOCK)
    {
        currentModifiers.scrollLock = !currentModifiers.scrollLock;
        setLeds(currentModifiers.capsLock, currentModifiers.numLock, currentModifiers.scrollLock);
    }

    event.modifiers = currentModifiers;
    event.ascii = mapAscii(scancode, currentModifiers);

    pushEvent(event);
}

Keyboard::Key Keyboard::mapKeys(const uint8_t scancode)
{
    const uint8_t makeCode = scancode & 0x7F;

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
            default: return Key::NONE;
        }

    if (prefixE1)
        switch (makeCode)
        {
            case 0x1D: return Key::PAUSE;
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
        default: return Key::NONE;
    }
}

char Keyboard::mapAscii(const uint8_t scancode, const Modifiers& modifiers)
{
    if (prefixE0 || prefixE1) return 0;
    const uint8_t makeCode = scancode & 0x7F;

    static constexpr char normalMap[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    static constexpr char shiftMap[128] = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    if (makeCode >= 128) return 0;
    const char normal = normalMap[makeCode];

    return normal
               ? normal >= 'a' && normal <= 'z'
                     ? modifiers.shift ^ modifiers.capsLock
                           ? static_cast<char>(normal - 'a' + 'A')
                           : normal
                     : modifiers.shift
                     ? shiftMap[makeCode]
                     : normal
               : 0;
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
