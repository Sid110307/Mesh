#pragma once

#include <core/utils.h>

namespace ACPI
{
    struct ISO
    {
        uint32_t globalIrq = 0;
        bool present = false, activeLow = false, levelTriggered = false;
    };

    struct MADTInfo
    {
        uint64_t ioapicPhys = 0, ioapicGlobalIrqBase = 0;
        ISO iso[16] = {};
    };

     bool init(MADTInfo& madtInfo);
     void resolveIsa(const MADTInfo& madtInfo, uint8_t src, uint32_t& globalIrq, bool& activeLow, bool& levelTriggered);
}
