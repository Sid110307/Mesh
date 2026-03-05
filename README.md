# Mesh

> A lightweight OS kernel with SMP, paging, and basic IO, written in C++ and x86 Assembly.

### Features

#### Current

- [x] x86_64 architecture
- [x] Limine bootloader
- [x] UEFI and BIOS support
- [x] SSE/SSE2 support
- [x] SMP and AP boot
- [x] GDT
- [x] Per-CPU TSS and IST stacks
- [x] IDT with exceptions and IRQ handlers
- [x] APIC, LAPIC, and IOAPIC support
- [x] PCI/ACPI support with RSDP/RSDT/XSDT parsing
- [x] 4-level paging (4KB/2MB/1GB)
- [x] Bitmap frame allocator
- [x] Spinlock
- [x] Atomic operations
- [x] Serial (UART) console output
- [x] Framebuffer text + ANSI color output
- [x] Keyboard driver (PS/2)
- [ ] Mouse driver (PS/2)
- [x] LAPIC timer interrupts and sleep functions
- [x] Kernel panic and stack trace
- [x] Kernel heap allocator (buddy and slab)
- [x] Per-CPU data structures
- [x] Kernel threads
- [x] Scheduler
- [x] Context switching

#### Planned

- [ ] Virtual memory management
- [ ] Process management and address spaces
- [ ] Memory protection and COW

#### Future (userland)

- [ ] Syscall interface
- [ ] ELF loader
- [ ] VFS layer
- [ ] Filesystem (FAT32, ext2, etc.)
- [ ] PCI enumeration and drivers
- [ ] Storage drivers (AHCI, NVMe, etc.)
- [ ] USB drivers
- [ ] Better terminal (scrolling, cursor, line editing, etc.)
- [ ] Framebuffer graphics primitives (2D/3D drawing, font rendering, etc.)
- [ ] Window compositor
- [ ] Networking (TCP/IP stack, Ethernet drivers, etc.)
- [ ] IPC event system
- [ ] SMP task scheduling and load balancing
- [ ] Power management (ACPI S3 sleep, etc.)
- [ ] RTC/CMOS clock support
- [ ] Other architectures (ARM, RISC-V, etc.)

## Requirements

- [NASM](https://www.nasm.us/)
- A C++ compiler ([GCC](https://gcc.gnu.org/), [Clang](https://clang.llvm.org/), etc.)
- [Binutils](https://www.gnu.org/software/binutils/)
- [Limine](https://limine-bootloader.org/)
- [CMake](https://cmake.org/)
- [QEMU](https://www.qemu.org/) (optional)

### Installing Limine

```bash
$ git clone --branch=v10.x https://github.com/limine-bootloader/limine.git
$ cd limine
$ ./bootstrap
$ ./configure
$ make
$ sudo make install
```

```bash
$ ./limine.sh
```

## Quick Start

- Clone the repository

```bash
$ git clone https://github.com/Sid110307/Mesh.git
$ cd Mesh
```

- Build and run on QEMU

```bash
$ ./run.sh
```

## License

[MIT](https://opensource.org/licenses/MIT)
