#include <kernel/arch/acpi.h>
#include <kernel/boot/limine.h>
#include <drivers/io/serial/serial.h>

extern limine_hhdm_request hhdm_request;
extern limine_rsdp_request rsdp_request;

static bool signaturesMatch(const char* sig1, const char* sig2, const size_t len)
{
    for (size_t i = 0; i < len; ++i) if (sig1[i] != sig2[i]) return false;
    return true;
}

static ACPI::SDTHeader* findTable(ACPI::SDTHeader* sdt, const char signature[4])
{
    const bool isXSDT = signaturesMatch(sdt->signature, "XSDT", 4);
    if (!isXSDT && !signaturesMatch(sdt->signature, "RSDT", 4)) return nullptr;

    const auto* base = reinterpret_cast<uint8_t*>(sdt) + sizeof(ACPI::SDTHeader);
    for (uint64_t i = 0; i < (sdt->length - sizeof(ACPI::SDTHeader)) / (isXSDT ? 8 : 4); ++i)
    {
        const uint64_t phys = isXSDT
                                  ? *reinterpret_cast<const uint64_t*>(base + i * 8)
                                  : *reinterpret_cast<const uint32_t*>(base + i * 4);
        if (!phys) continue;

        auto* hdr = reinterpret_cast<ACPI::SDTHeader*>(phys + hhdm_request.response->offset);
        if (hdr && signaturesMatch(hdr->signature, signature, 4)) return hdr;
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
        Serial::printf("ACPI: No valid RSDT/XSDT address in RSDP (revision %u, RSDT 0x%X, XSDT 0x%lX)\n",
                       rsdp->revision, rsdp->rsdtAddress, rsdp->xsdtAddress);
        return false;
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
