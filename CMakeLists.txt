cmake_minimum_required(VERSION 3.20)
project(Mesh LANGUAGES C CXX ASM_NASM)

find_program(ASM_NASM_EXECUTABLE nasm)
if (NOT ASM_NASM_EXECUTABLE)
    message(FATAL_ERROR "NASM not found. Please install nasm.")
endif ()

find_program(XORRISO_EXECUTABLE xorriso)
if (NOT XORRISO_EXECUTABLE)
    message(FATAL_ERROR "xorriso not found. Please install xorriso.")
endif ()

find_program(LIMINE_EXECUTABLE limine)
find_path(LIMINE_ROOT BOOTX64.EFI PATHS /usr/local/share/limine PATH_SUFFIXES share/limine)
find_file(LIMINE_UEFI_BIN NAMES limine-uefi-cd.bin PATHS ${LIMINE_ROOT})
find_file(LIMINE_BIOS_BIN NAMES limine-bios-cd.bin PATHS ${LIMINE_ROOT})
find_file(LIMINE_BIOS_SYS NAMES limine-bios.sys PATHS ${LIMINE_ROOT})
if (NOT LIMINE_EXECUTABLE OR NOT LIMINE_ROOT OR NOT LIMINE_UEFI_BIN OR NOT LIMINE_BIOS_BIN OR NOT LIMINE_BIOS_SYS)
    message(FATAL_ERROR "Limine not found. Please build and install Limine.")
endif ()

find_program(OBJCOPY_EXECUTABLE objcopy)
if (NOT OBJCOPY_EXECUTABLE)
    message(FATAL_ERROR "objcopy not found. Please install binutils.")
endif ()

find_program(QEMU_EXECUTABLE qemu-system-x86_64)
if (NOT QEMU_EXECUTABLE)
    message(WARNING "qemu-system-x86_64 not found. Target \"run\" will not work.")
endif ()

find_file(OVMF_CODE_FILE NAMES OVMF_CODE.fd OVMF_CODE_4M.fd PATHS /usr/share/OVMF /usr/share/ovmf /usr/share/edk2/x64)
find_file(OVMF_VARS_FILE NAMES OVMF_VARS.fd OVMF_VARS_4M.fd PATHS /usr/share/OVMF /usr/share/ovmf /usr/share/edk2/x64)
if (NOT OVMF_CODE_FILE OR NOT OVMF_VARS_FILE)
    message(FATAL_ERROR "OVMF files not found. Please install OVMF.")
endif ()
configure_file(${OVMF_VARS_FILE} ${CMAKE_BINARY_DIR}/OVMF_VARS.fd COPYONLY)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(OUT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(ISO_ROOT ${CMAKE_BINARY_DIR}/iso)

file(GLOB_RECURSE ASM_NASM_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/**/*.asm")
file(GLOB_RECURSE C_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/**/*.c")
file(GLOB_RECURSE CPP_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/**/*.cpp")

file(GLOB_RECURSE EMBED_ASSETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/assets/*")
set(EMBED_ASSET_OBJS "")

foreach (asset ${EMBED_ASSETS})
    file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${asset}")

    string(REPLACE "/" "_" base "${rel_path}")
    string(REPLACE "." "_" base "${base}")
    string(REPLACE "-" "_" base "${base}")
    set(symbol_base "asset_${base}")

    string(REPLACE "/" "_" object "${asset}")
    string(REPLACE "." "_" object "${object}")
    string(REPLACE "-" "_" orig "${object}")
    set(obj_file "${CMAKE_BINARY_DIR}/${symbol_base}.o")

    add_custom_command(
        OUTPUT ${obj_file}
        COMMAND ${OBJCOPY_EXECUTABLE} -I binary -O elf64-x86-64 -B i386:x86-64 ${asset} ${obj_file}
        COMMAND ${OBJCOPY_EXECUTABLE} --redefine-sym _binary_${orig}_start=${symbol_base}_start ${obj_file}
        COMMAND ${OBJCOPY_EXECUTABLE} --redefine-sym _binary_${orig}_end=${symbol_base}_end ${obj_file}
        COMMAND ${OBJCOPY_EXECUTABLE} --redefine-sym _binary_${orig}_size=${symbol_base}_size ${obj_file}
        DEPENDS ${asset}
        COMMENT "Embedding asset ${rel_path}"
    )
    list(APPEND EMBED_ASSET_OBJS ${obj_file})
endforeach ()

add_custom_target(embedded_assets ALL DEPENDS ${EMBED_ASSET_OBJS})
add_executable(mesh.elf ${ASM_NASM_SRC} ${C_SRC} ${CPP_SRC} ${EMBED_ASSET_OBJS})
add_dependencies(mesh.elf embedded_assets)

set(COMMON_C_FLAGS -ffreestanding -g -O2 -Wall -Wextra -m64 -mno-red-zone -mgeneral-regs-only -fno-omit-frame-pointer)
target_compile_options(mesh.elf PRIVATE $<$<COMPILE_LANGUAGE:C>:${COMMON_C_FLAGS}>
    $<$<COMPILE_LANGUAGE:CXX>:${COMMON_C_FLAGS} -fno-exceptions -fno-rtti>
    $<$<COMPILE_LANGUAGE:ASM_NASM>:-f elf64>)
target_include_directories(mesh.elf PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
target_compile_definitions(mesh.elf PRIVATE LIMINE_API_REVISION=3)
target_link_options(mesh.elf PRIVATE -nostdlib -z noexecstack -static -T ${CMAKE_SOURCE_DIR}/lib/linker.ld)

add_custom_command(
    OUTPUT ${OUT_DIR}/Mesh.iso
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_ROOT}/EFI/BOOT
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_ROOT}/boot
    COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_UEFI_BIN} ${ISO_ROOT}/limine-uefi-cd.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_BIOS_BIN} ${ISO_ROOT}/limine-bios-cd.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_BIOS_SYS} ${ISO_ROOT}/limine-bios.sys
    COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_ROOT}/BOOTX64.EFI ${ISO_ROOT}/EFI/BOOT/BOOTX64.EFI
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lib/limine.conf ${ISO_ROOT}/limine.conf
    COMMAND ${CMAKE_COMMAND} -E copy ${OUT_DIR}/mesh.elf ${ISO_ROOT}/mesh.elf
    COMMAND ${XORRISO_EXECUTABLE} -as mkisofs -R -r -o ${OUT_DIR}/Mesh.iso -b limine-bios-cd.bin -no-emul-boot
    -boot-load-size 4 -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image ${ISO_ROOT}
    >/dev/null 2>&1
    COMMAND ${LIMINE_EXECUTABLE} bios-install ${OUT_DIR}/Mesh.iso >/dev/null 2>&1
    DEPENDS mesh.elf
    COMMENT "Creating bootable ISO"
)
add_custom_target(iso DEPENDS ${OUT_DIR}/Mesh.iso)
add_custom_target(run
    COMMAND ${QEMU_EXECUTABLE} -machine q35 -m 512M -smp 4 -serial mon:stdio -no-reboot -s -cdrom ${OUT_DIR}/Mesh.iso
    -drive if=pflash,format=raw,readonly=on,file=${OVMF_CODE_FILE}
    -drive if=pflash,format=raw,file=${CMAKE_BINARY_DIR}/OVMF_VARS.fd
    DEPENDS iso
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Mesh in QEMU"
)
