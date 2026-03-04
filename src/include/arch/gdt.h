#pragma once

#include <core/utils.h>

namespace GDTManager
{
    void init();
    void load();
    void setTSS(size_t cpuIndex, uint64_t rsp0);
    void loadTR(size_t cpuIndex);
}
