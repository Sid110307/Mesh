#include <arch/x86_64/acpi.h>
#include <arch/x86_64/lapic.h>
#include <core/limine.h>
#include <drivers/serial.h>
#include <memory/paging.h>

struct __attribute__ ((packed)) RSDP
{
    char signature[8];
    uint8_t checksum;
    char oemId[6];
    uint8_t revision;
    uint32_t rsdtAddress, length;
    uint64_t xsdtAddress;
    uint8_t extChecksum, reserved[3];
};

struct __attribute__ ((packed)) SDTHeader
{
    char signature[4];
    uint32_t length;
    uint8_t revision, checksum;
    char oemId[6], oemTableId[8];
    uint32_t oemRevision, creatorId, creatorRevision;
};

struct __attribute__ ((packed)) MADT
{
    SDTHeader header;
    uint32_t lapicAddr, flags;
};

struct __attribute__ ((packed)) MADTEntryHeader
{
    uint8_t type, length;
};

struct __attribute__ ((packed)) MADT_IOAPIC
{
    MADTEntryHeader header;
    uint8_t ioapicId, reserved;
    uint32_t ioapicAddr, globalIrqBase;
};

struct __attribute__ ((packed)) MADT_ISO
{
    MADTEntryHeader header;
    uint8_t bus, source;
    uint32_t globalIrq;
    uint16_t flags;
};

struct __attribute__ ((packed)) FADT_GAS
{
    uint8_t addressSpace, bitWidth, bitOffset, accessSize;
    uint64_t address;
};

struct __attribute__ ((packed)) FADT
{
    SDTHeader header;
    uint32_t firmwareCtrl, dsdtAddress;
    uint8_t reserved1, preferredPmProfile;
    uint16_t sciInterrupt;
    uint32_t smiCommandPort;
    uint8_t acpiEnable, acpiDisable, s4biosReq, pStateControl;
    uint32_t pm1aEventBlockAddr, pm1bEventBlockAddr, pm1aControlBlockAddr, pm1bControlBlockAddr, pm2ControlBlockAddr,
             pmTimerBlockAddr, gpe0BlockAddr, gpe1BlockAddr;
    uint8_t pm1EventLength, pm1ControlLength, pm2ControlLength, pmTimerLength, gpe0Length, gpe1Length, gpe1Base,
            cStateControl;
    uint16_t worstC2Latency, worstC3Latency, flushSize, flushStride;
    uint8_t dutyOffset, dutyWidth, dayAlarm, monthAlarm, centuryAlarm;
    uint16_t bootArchitectureFlags;
    uint8_t reserved2;
    uint32_t flags;
    FADT_GAS resetReg;
    uint8_t resetValue, reserved3[3];
    uint64_t xFirmwareCtrl, xDsdtAddress;
    FADT_GAS xPm1aEventBlock, xPm1bEventBlock, xPm1aControlBlock, xPm1bControlBlock, xPm2ControlBlock, xPmTimerBlock,
             xGpe0Block, xGpe1Block;
};

extern limine_hhdm_request hhdm_request;
extern limine_rsdp_request rsdp_request;

void* mapAcpi(const uint64_t physical, const uint64_t length)
{
    if (!physical || !length || length > 16 * 1024 * 1024 || physical > UINT64_MAX - length) return nullptr;

    const uint64_t base = Alignment::alignDown(physical, FrameAllocator::SMALL_SIZE),
                   end = Alignment::alignUp(physical + length, FrameAllocator::SMALL_SIZE);

    for (uint64_t p = base; p < end; p += FrameAllocator::SMALL_SIZE)
        if (!Paging::map(p + hhdm_request.response->offset, p, FrameAllocator::SMALL_SIZE,
                         PageFlags::PRESENT | PageFlags::GLOBAL | PageFlags::NO_EXECUTE))
            return nullptr;
    return reinterpret_cast<void*>(physical + hhdm_request.response->offset);
}

SDTHeader* mapSdt(const uint64_t physical)
{
    const auto* header = static_cast<SDTHeader*>(mapAcpi(physical, sizeof(SDTHeader)));
    if (!header || header->length < sizeof(SDTHeader)) return nullptr;

    return static_cast<SDTHeader*>(mapAcpi(physical, header->length));
}

bool signaturesMatch(const char* sig1, const char* sig2, const size_t len)
{
    for (size_t i = 0; i < len; ++i) if (sig1[i] != sig2[i]) return false;
    return true;
}

bool checksumValid(const uint8_t* data, const size_t length)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) sum += data[i];

    return sum == 0;
}

SDTHeader* findTable(SDTHeader* sdt, const char signature[4])
{
    const bool isXSDT = signaturesMatch(sdt->signature, "XSDT", 4);
    if (!isXSDT && !signaturesMatch(sdt->signature, "RSDT", 4)) return nullptr;

    const auto* base = reinterpret_cast<uint8_t*>(sdt) + sizeof(SDTHeader);
    for (uint64_t i = 0; i < (sdt->length - sizeof(SDTHeader)) / (isXSDT ? 8 : 4); ++i)
    {
        const uint64_t phys = isXSDT
                                  ? *reinterpret_cast<const uint64_t*>(base + i * 8)
                                  : *reinterpret_cast<const uint32_t*>(base + i * 4);
        if (!phys) continue;

        auto* hdr = mapSdt(phys);
        if (hdr && signaturesMatch(hdr->signature, signature, 4))
        {
            if (!checksumValid(reinterpret_cast<const uint8_t*>(hdr), hdr->length))
            {
                Serial::printf("ACPI: Invalid checksum for table %s\n", signature);
                continue;
            }
            return hdr;
        }
    }
    return nullptr;
}

bool ACPI::init(MADTInfo& madtInfo)
{
    if (!rsdp_request.response)
    {
        Serial::printf("ACPI: RSDP not found\n");
        return false;
    }

    uint64_t physical = rsdp_request.response->address;
    if (physical >= hhdm_request.response->offset) physical -= hhdm_request.response->offset;
    const auto* rsdp = static_cast<RSDP*>(mapAcpi(physical, 20));
    if (!rsdp)
    {
        Serial::printf("ACPI: Failed to map RSDP\n");
        return false;
    }

    if (rsdp->revision >= 2)
    {
        rsdp = static_cast<RSDP*>(mapAcpi(physical, sizeof(RSDP)));
        if (!rsdp || rsdp->length < sizeof(RSDP) || rsdp->length > 4096)
        {
            Serial::printf("ACPI: Invalid RSDP length %u\n", rsdp ? rsdp->length : 0);
            return false;
        }

        rsdp = static_cast<RSDP*>(mapAcpi(physical, rsdp->length));
        if (!rsdp)
        {
            Serial::printf("ACPI: Failed to map RSDP\n");
            return false;
        }
    }
    if (!signaturesMatch(rsdp->signature, "RSD PTR ", 8))
    {
        Serial::printf("ACPI: Invalid RSDP signature\n");
        return false;
    }
    if (!checksumValid(reinterpret_cast<const uint8_t*>(rsdp), rsdp->revision >= 2 ? rsdp->length : 20))
    {
        Serial::printf("ACPI: Invalid RSDP checksum\n");
        return false;
    }

    SDTHeader* root = nullptr;
    if (rsdp->revision >= 2 && rsdp->xsdtAddress)
    {
        root = mapSdt(rsdp->xsdtAddress);
        if (!root)
        {
            Serial::printf("ACPI: Failed to map XSDT\n");
            return false;
        }
        if (!signaturesMatch(root->signature, "XSDT", 4)) Serial::printf("ACPI: Invalid XSDT signature\n");
    }
    else if (rsdp->rsdtAddress)
    {
        root = mapSdt(rsdp->rsdtAddress);
        if (!root)
        {
            Serial::printf("ACPI: Failed to map RSDT\n");
            return false;
        }
        if (!signaturesMatch(root->signature, "RSDT", 4)) Serial::printf("ACPI: Invalid RSDT signature\n");
    }
    else
    {
        Serial::printf("ACPI: No valid RSDT/XSDT address in RSDP (revision %u, RSDT 0x%x, XSDT 0x%lx)\n",
                       rsdp->revision, rsdp->rsdtAddress, rsdp->xsdtAddress);
        return false;
    }

    if (!checksumValid(reinterpret_cast<const uint8_t*>(root), root->length)) return false;
    if (const auto* header = findTable(root, "FACP"))
        if (const auto* fadt = reinterpret_cast<const FADT*>(header);
            header->length >= offsetof(FADT, flags) + sizeof(fadt->flags))
        {
            const bool is32Bit = (fadt->flags & (1u << 8)) != 0;
            bool configured = false;

            if (header->length >= offsetof(FADT, xPmTimerBlock) + sizeof(FADT_GAS))
                if (const auto& gas = fadt->xPmTimerBlock; gas.address && gas.bitOffset == 0)
                {
                    if (gas.addressSpace == 1 && gas.address <= 0xFFFC)
                    {
                        LAPIC::timerSetPort(static_cast<uint32_t>(gas.address), is32Bit);
                        configured = true;
                    }
                    else if (gas.addressSpace == 0)
                    {
                        if (const uint64_t base = Alignment::alignDown(gas.address, FrameAllocator::SMALL_SIZE);
                            !Paging::map(base + hhdm_request.response->offset, base,
                                         Alignment::alignUp(gas.address - base + 4, FrameAllocator::SMALL_SIZE),
                                         PageFlags::PRESENT | PageFlags::RW | PageFlags::CACHE_DISABLE |
                                         PageFlags::NO_EXECUTE))
                        {
                            Serial::printf("ACPI: Failed to map PM timer GAS\n");
                            return false;
                        }

                        LAPIC::timerSetMemory(gas.address + hhdm_request.response->offset, is32Bit);
                        configured = true;
                    }
                }
            if (!configured && fadt->pmTimerBlockAddr && fadt->pmTimerBlockAddr <= 0xFFFC)
                LAPIC::timerSetPort(fadt->pmTimerBlockAddr, is32Bit);
        }

    auto* madtHeader = findTable(root, "APIC");
    if (!madtHeader)
    {
        Serial::printf("ACPI: MADT not found\n");
        return false;
    }

    if (madtHeader->length < sizeof(MADT))
    {
        Serial::printf("ACPI: Invalid MADT length\n");
        return false;
    }
    auto* madt = reinterpret_cast<MADT*>(madtHeader);
    const uint8_t *start = reinterpret_cast<uint8_t*>(madt) + sizeof(MADT),
                  *end = reinterpret_cast<uint8_t*>(madt) + madt->header.length;

    while (start + sizeof(MADTEntryHeader) <= end)
    {
        auto* entryHeader = reinterpret_cast<const MADTEntryHeader*>(start);
        if (entryHeader->length < sizeof(MADTEntryHeader) || start + entryHeader->length > end)
        {
            Serial::printf("ACPI: Invalid MADT entry length\n");
            return false;
        }

        if (entryHeader->type == 1 && entryHeader->length >= sizeof(MADT_IOAPIC))
        {
            if (madtInfo.ioapicCount >= MAX_IOAPIC)
            {
                Serial::printf("ACPI: Too many IOAPIC entries in MADT (max %d)\n", MAX_IOAPIC);
                return false;
            }

            auto* ioapicEntry = reinterpret_cast<const MADT_IOAPIC*>(start);
            const int n = madtInfo.ioapicCount++;

            madtInfo.ioapicPhys[n] = ioapicEntry->ioapicAddr;
            madtInfo.ioapicGlobalIrqBase[n] = ioapicEntry->globalIrqBase;
        }
        else if (entryHeader->type == 2 && entryHeader->length >= sizeof(MADT_ISO))
            if (auto* isoEntry = reinterpret_cast<const MADT_ISO*>(start); isoEntry->bus == 0 && isoEntry->source < 16)
            {
                madtInfo.iso[isoEntry->source].present = true;
                madtInfo.iso[isoEntry->source].globalIrq = isoEntry->globalIrq;
                madtInfo.iso[isoEntry->source].activeLow = (isoEntry->flags & 0x3) == 3;
                madtInfo.iso[isoEntry->source].levelTriggered = ((isoEntry->flags >> 2) & 0x3) == 3;
            }

        start += entryHeader->length;
    }

    if (!madtInfo.ioapicCount)
    {
        Serial::printf("ACPI: No IOAPIC entry found in MADT\n");
        return false;
    }
    return true;
}

void ACPI::resolveIsa(const MADTInfo& madtInfo, const uint8_t src, uint32_t& globalIrq, bool& activeLow,
                      bool& levelTriggered)
{
    if (src < 16 && madtInfo.iso[src].present)
    {
        globalIrq = madtInfo.iso[src].globalIrq;
        activeLow = madtInfo.iso[src].activeLow;
        levelTriggered = madtInfo.iso[src].levelTriggered;
    }
    else
    {
        globalIrq = src;
        activeLow = false;
        levelTriggered = false;
    }
}
