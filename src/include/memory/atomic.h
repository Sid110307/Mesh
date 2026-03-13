#pragma once

template <typename T>
class Atomic
{
public:
    explicit Atomic(T init = 0);

    [[nodiscard]] T load() const noexcept;
    void store(T val) noexcept;
    T increment() noexcept;
    bool compareExchange(T& expected, T desired) noexcept;

private:
    T value;
};
