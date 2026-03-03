#pragma once

#include <kernel/core/utils.h>

class ACPI
{
public:
    struct Iso
    {
        uint32_t globalIrq = 0;
        bool present = false, activeLow = false, levelTriggered = false;
    };

    struct MADTInfo
    {
        uint64_t ioapicPhys = 0, ioapicGlobalIrqBase = 0;
        Iso iso[16] = {};
    };

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

    static bool init(MADTInfo& madtInfo);
    static void resolveIsa(const MADTInfo& madtInfo, uint8_t src, uint32_t& globalIrq, bool& activeLow, bool& levelTriggered);
};
