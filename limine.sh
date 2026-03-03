#!/usr/bin/env bash

sudo apt-get update
sudo apt-get install -y nasm cmake qemu-system-x86 xorriso binutils
git clone --branch v10.x-binary https://github.com/limine-bootloader/limine.git --depth=1
make -C limine

sudo install -d /usr/local/share/limine
sudo install -d /usr/local/bin

sudo install -m 644 limine/limine-bios.sys /usr/local/share/limine/ || true
sudo install -m 644 limine/limine-bios-cd.bin /usr/local/share/limine/ || true
sudo install -m 644 limine/limine-uefi-cd.bin /usr/local/share/limine/ || true
sudo install -m 644 limine/limine-bios-pxe.bin /usr/local/share/limine/ || true
sudo install -m 644 limine/BOOTX64.EFI /usr/local/share/limine/ || true
sudo install -m 644 limine/BOOTIA32.EFI /usr/local/share/limine/ || true
sudo install -m 644 limine/BOOTAA64.EFI /usr/local/share/limine/ || true
sudo install -m 644 limine/BOOTRISCV64.EFI /usr/local/share/limine/ || true
sudo install -m 644 limine/BOOTLOONGARCH64.EFI /usr/local/share/limine/ || true
sudo install -m 755 limine/limine /usr/local/bin/
