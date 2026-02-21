#pragma once

#include <core/utils.h>

class alignas(4) Atomic
{
public:
    explicit Atomic(uint32_t init = 0);

    [[nodiscard]] uint32_t load() const noexcept;
    void store(uint32_t val) noexcept;
    uint32_t increment() noexcept;
    bool compareExchange(uint32_t& expected, uint32_t desired) noexcept;

private:
    volatile uint32_t value;
};
