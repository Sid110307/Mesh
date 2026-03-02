#pragma once

#include <kernel/core/utils.h>

class ACPI
{
public:
    struct MADTInfo
    {
        uint64_t ioapicPhys = 0, ioapicGlobalIrqBase = 0;
        bool hasIso = false;
        uint32_t irq1GlobalIrqBase = 1;
        bool irq1ActiveLow = false, irq1LevelTriggered = false;
    };

    struct __attribute__((packed)) RSDP
    {
        char signature[8];
        uint8_t checksum;
        char oemId[6];
        uint8_t revision;
        uint32_t rsdtAddress, length;
        uint64_t xsdtAddress;
        uint8_t extChecksum, reserved[3];
    };

    struct __attribute__((packed)) SDTHeader
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

    struct __attribute__((packed)) MADT_ISO
    {
        MADTEntryHeader header;
        uint8_t bus, source;
        uint32_t globalIrq;
        uint16_t flags;
    };

    static bool init(MADTInfo& madtInfo);
};
