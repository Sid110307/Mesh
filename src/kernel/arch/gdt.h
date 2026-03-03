#pragma once

#include <kernel/core/utils.h>

namespace GDTManager
{
    void init();
    void load();
    void setTSS(size_t cpuIndex, uint64_t rsp0);
    void loadTR(size_t cpuIndex);
}
