# Mesh

> A lightweight OS kernel with SMP, paging, and basic IO, written in C++ and x86 Assembly.

### Features

#### Current

- [x] x86_64 architecture
- [x] Limine bootloader
- [ ] SMP and AP boot
- [x] GDT, per-CPU TSS, and IST
- [x] IDT with exceptions and interrupts
- [x] LAPIC and APIC support
- [x] Spinlock
- [x] Atomic operations
- [x] 4-level paging (4KB/2MB/1GB)
- [x] Thread-safe page map/unmap
- [x] Bitmap frame allocator
- [x] Serial (UART) console output
- [x] Framebuffer text + ANSI color output

#### Planned

- [ ] Keyboard driver (PS/2, USB)
- [ ] PIT Timer interrupts
- [ ] Kernel panic and stack trace
- [ ] Scheduler
- [ ] Virtual memory management
- [ ] Memory protection and COW

#### Future

- [ ] ELF loader
- [ ] Filesystem (FAT32, ext2, etc.)
- [ ] PCI/ACPI support
- [ ] Basic network stack (TCP/IP)
- [ ] IPC/event system
- [ ] Multitasking
- [ ] Userspace

## Requirements

- [NASM](https://www.nasm.us/)
- A C++ compiler ([GCC](https://gcc.gnu.org/), [Clang](https://clang.llvm.org/), etc.)
- [LD](https://www.gnu.org/software/binutils/)
- [Limine](https://limine-bootloader.org/)
- [CMake](https://cmake.org/)
- [QEMU](https://www.qemu.org/) (optional)

### Installing Limine

```bash
$ git clone --branch=v9.x https://github.com/limine-bootloader/limine.git
$ cd limine
$ ./bootstrap
$ ./configure
$ make
$ sudo make install
```

```bash
$ git clone --branch=v9.x-binary https://github.com/limine-bootloader/limine.git limine-binary
$ cd limine-binary
$ make
$ sudo make install
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
