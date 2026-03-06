#include <arch/x86_64/acpi.h>
#include <arch/x86_64/lapic.h>
#include <core/limine.h>
#include <drivers/serial.h>

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

        auto* hdr = reinterpret_cast<SDTHeader*>(phys + hhdm_request.response->offset);
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

    const auto* rsdp = reinterpret_cast<RSDP*>(rsdp_request.response->address);
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
        root = reinterpret_cast<SDTHeader*>(rsdp->xsdtAddress + hhdm_request.response->offset);
        if (!signaturesMatch(root->signature, "XSDT", 4)) Serial::printf("ACPI: Invalid XSDT signature\n");
    }
    else if (rsdp->rsdtAddress)
    {
        root = reinterpret_cast<SDTHeader*>(static_cast<uint64_t>(rsdp->rsdtAddress) + hhdm_request.response->offset);
        if (!signaturesMatch(root->signature, "RSDT", 4)) Serial::printf("ACPI: Invalid RSDT signature\n");
    }
    else
    {
        Serial::printf("ACPI: No valid RSDT/XSDT address in RSDP (revision %u, RSDT 0x%x, XSDT 0x%lx)\n",
                       rsdp->revision, rsdp->rsdtAddress, rsdp->xsdtAddress);
        return false;
    }

    if (auto* facpHeader = findTable(root, "FACP"); !facpHeader) Serial::printf("ACPI: FACP not found\n");
    else
    {
        if (const auto* fadt = reinterpret_cast<FADT*>(facpHeader); fadt->xPmTimerBlock.address)
        {
            if (fadt->xPmTimerBlock.addressSpace == 1 && fadt->xPmTimerBlock.address <= 0xFFFFFFFFu)
                LAPIC::timerSetPort(static_cast<uint32_t>(fadt->xPmTimerBlock.address),
                                    fadt->xPmTimerBlock.bitWidth == 32);
            else
                Serial::printf("ACPI: Invalid PM timer GAS in FADT (address space %u, address 0x%lx, bit width %u)\n",
                               fadt->xPmTimerBlock.addressSpace, fadt->xPmTimerBlock.address,
                               fadt->xPmTimerBlock.bitWidth);
        }
        else if (fadt->pmTimerBlockAddr) LAPIC::timerSetPort(fadt->pmTimerBlockAddr, fadt->pmTimerLength == 4);
        else Serial::printf("ACPI: No PM timer block address found in FADT\n");
    }

    auto* madtHeader = findTable(root, "APIC");
    if (!madtHeader)
    {
        Serial::printf("ACPI: MADT not found\n");
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
            auto* ioapicEntry = reinterpret_cast<const MADT_IOAPIC*>(start);
            madtInfo.ioapicPhys = ioapicEntry->ioapicAddr;
            madtInfo.ioapicGlobalIrqBase = ioapicEntry->globalIrqBase;
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

    if (!madtInfo.ioapicPhys)
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
