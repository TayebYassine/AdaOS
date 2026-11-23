#!/usr/bin/env bash
# AdaOS - build the kernel and run it on QEMU.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"

mkdir -p "$BUILD"

echo "[1/4] Assembling .asm sources..."
# Assemble every .asm file in src/ into a matching .o in build/
OBJECTS=()
for src in "$SRC"/*.asm; do
    obj="$BUILD/$(basename "${src%.asm}.o")"
    nasm -f elf32 "$src" -o "$obj"
    OBJECTS+=("$obj")
done

echo "[2/4] Compiling C sources..."
# Compile every .c file in src/ into a matching .o in build/
for src in "$SRC"/*.c; do
    obj="$BUILD/$(basename "${src%.c}.o")"
    # -fno-builtin: we define strlen/memcpy/... ourselves in libc.c
    # -fno-asynchronous-unwind-tables: no .eh_frame, nothing unwinds in a kernel
    gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done

echo "[3/4] Linking kernel.elf..."
ld -m elf_i386 -T "$SRC/linker.ld" \
    -o "$BUILD/kernel.elf" "${OBJECTS[@]}"

echo "[4/4] Booting on QEMU..."
# Any extra arguments you pass to build.sh are forwarded to QEMU,
# e.g.:  ./build.sh -display curses   or   ./build.sh -no-reboot
exec qemu-system-i386 -kernel "$BUILD/kernel.elf" "$@"
