cmake_minimum_required(VERSION 3.20)
project(Mesh LANGUAGES C CXX ASM_NASM)

find_program(ASM_NASM_EXECUTABLE nasm)
if (NOT ASM_NASM_EXECUTABLE)
    message(FATAL_ERROR "❌ NASM not found. Please install nasm.")
endif ()

find_program(XORRISO_EXECUTABLE xorriso)
if (NOT XORRISO_EXECUTABLE)
    message(FATAL_ERROR "❌ xorriso not found. Please install xorriso.")
endif ()

find_program(LIMINE_EXECUTABLE limine)
if (NOT LIMINE_EXECUTABLE)
    message(FATAL_ERROR "❌ Limine not found. Please build and install Limine.")
endif ()

find_program(OBJCOPY_EXECUTABLE objcopy)
if (NOT OBJCOPY_EXECUTABLE)
    message(FATAL_ERROR "❌ objcopy not found. Please install binutils.")
endif ()

find_program(QEMU_EXECUTABLE qemu-system-x86_64)
if (NOT QEMU_EXECUTABLE)
    message(WARNING "⚠️ qemu-system-x86_64 not found. Target \"run\" will not work.")
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(OUT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(ISO_ROOT ${CMAKE_BINARY_DIR}/iso)
set(LIMINE_ROOT /usr/local/share/limine)

set(CMAKE_C_FLAGS "-ffreestanding -O2 -Wall -Wextra -m64 -mno-red-zone")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-exceptions -fno-rtti")
set(CMAKE_ASM_NASM_FLAGS "-f elf64")
set(CMAKE_EXE_LINKER_FLAGS "-nostdlib -z noexecstack -static -T ${CMAKE_SOURCE_DIR}/lib/linker.ld")

include_directories(${CMAKE_SOURCE_DIR}/src)
file(GLOB_RECURSE ASM_NASM_SRC "${CMAKE_SOURCE_DIR}/src/**/*.asm")
file(GLOB_RECURSE C_SRC "${CMAKE_SOURCE_DIR}/src/**/*.c")
file(GLOB_RECURSE CPP_SRC "${CMAKE_SOURCE_DIR}/src/**/*.cpp")

file(GLOB_RECURSE EMBED_ASSETS "${CMAKE_SOURCE_DIR}/src/assets/*")
set(EMBED_ASSET_OBJS "")

foreach (asset ${EMBED_ASSETS})
    file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}" "${asset}")
    string(REPLACE "/" "_" symbol_name "${rel_path}")
    string(REPLACE "." "_" symbol_name "${symbol_name}")
    string(REPLACE "-" "_" symbol_name "${symbol_name}")
    set(obj_file "${CMAKE_BINARY_DIR}/${symbol_name}.o")

    add_custom_command(
            OUTPUT ${obj_file}
            COMMAND ${OBJCOPY_EXECUTABLE} --input-target=binary --output-target=elf64-x86-64 --binary-architecture=i386 --rename-section .data=.assets,alloc,load,readonly,data ${asset} ${obj_file}
            DEPENDS ${asset}
            COMMENT "📦 Embedding ${rel_path}"
    )
    list(APPEND EMBED_ASSET_OBJS ${obj_file})
endforeach ()

add_custom_target(embedded_assets ALL DEPENDS ${EMBED_ASSET_OBJS})
add_executable(mesh.elf ${ASM_NASM_SRC} ${C_SRC} ${CPP_SRC} ${EMBED_ASSET_OBJS})
add_dependencies(mesh.elf embedded_assets)

add_custom_command(
        OUTPUT ${OUT_DIR}/Mesh.iso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_ROOT}/boot/limine
        COMMAND ${CMAKE_COMMAND} -E copy ${OUT_DIR}/mesh.elf ${ISO_ROOT}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lib/limine.conf ${ISO_ROOT}/boot/limine/limine.conf
        COMMAND ${CMAKE_COMMAND} -E copy ${LIMINE_ROOT}/limine-bios.sys ${LIMINE_ROOT}/limine-bios-cd.bin ${LIMINE_ROOT}/limine-uefi-cd.bin ${ISO_ROOT}/boot/limine
        COMMAND ${XORRISO_EXECUTABLE} -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label ${ISO_ROOT} -o ${OUT_DIR}/Mesh.iso
        COMMAND ${LIMINE_EXECUTABLE} bios-install ${OUT_DIR}/Mesh.iso
        DEPENDS mesh.elf
        COMMENT "📀 Creating bootable ISO"
)
add_custom_target(iso ALL DEPENDS ${OUT_DIR}/Mesh.iso)
add_custom_target(run
        COMMAND ${QEMU_EXECUTABLE} -cdrom ${OUT_DIR}/Mesh.iso -serial mon:stdio
        DEPENDS iso
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "🚀 Running Mesh in QEMU..."
)
