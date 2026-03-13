#pragma once

#include <core/utils.h>

namespace ACPI
{
    constexpr int MAX_IOAPIC = 4;

    struct ISO
    {
        uint32_t globalIrq = 0;
        bool present = false, activeLow = false, levelTriggered = false;
    };

    struct MADTInfo
    {
        uint32_t ioapicPhys[MAX_IOAPIC] = {}, ioapicGlobalIrqBase[MAX_IOAPIC] = {};
        int ioapicCount = {};
        ISO iso[16] = {};
    };

    bool init(MADTInfo& madtInfo);
    void resolveIsa(const MADTInfo& madtInfo, uint8_t src, uint32_t& globalIrq, bool& activeLow, bool& levelTriggered);
}
