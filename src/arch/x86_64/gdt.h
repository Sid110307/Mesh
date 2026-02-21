#pragma once

#include <core/utils.h>

struct __attribute__ ((packed)) GDTEntry
{
    uint16_t limitLow, baseLow;
    uint8_t baseMid, access, flagsLimitHigh, baseHigh;
};

struct __attribute__ ((packed)) GDTPointer
{
    uint16_t limit;
    uint64_t base;
};

struct __attribute__ ((packed)) TSS
{
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, ioMapBase;
};

class GDTManager
{
public:
    GDTManager();
    static void load();
    static void setTSS(size_t cpuIndex, uint64_t rsp0);

private:
    static void setEntry(uint16_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);
};
