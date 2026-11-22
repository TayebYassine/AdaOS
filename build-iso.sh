#!/usr/bin/env bash
# AdaOS - build a bootable GRUB ISO for VirtualBox.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
ISO_DIR="$ROOT/iso"
ISO_OUT="$ROOT/adaos.iso"

mkdir -p "$BUILD"

echo "[1/4] Building kernel.elf..."
# Assemble every .asm file in src/ into a matching .o in build/
OBJECTS=()
for src in "$SRC"/*.asm; do
    obj="$BUILD/$(basename "${src%.asm}.o")"
    nasm -f elf32 "$src" -o "$obj"
    OBJECTS+=("$obj")
done
# Compile every .c file in src/ into a matching .o in build/
for src in "$SRC"/*.c; do
    obj="$BUILD/$(basename "${src%.c}.o")"
    # -fno-builtin: we define strlen/memcpy/... ourselves in libc.c
    gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done
ld -m elf_i386 -T "$SRC/linker.ld" \
    -o "$BUILD/kernel.elf" "${OBJECTS[@]}"

echo "[2/4] Staging ISO tree..."
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub"
cp "$BUILD/kernel.elf" "$ISO_DIR/boot/"

echo "[3/4] Writing GRUB config..."
cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=0
set default=0

menuentry "AdaOS" {
    multiboot /boot/kernel.elf
    boot
}
EOF

echo "[4/4] Creating ISO with grub-mkrescue..."
grub-mkrescue -o "$ISO_OUT" "$ISO_DIR"

echo "Done: $ISO_OUT"
echo "In VirtualBox: create a VM -> Storage -> add this ISO as an optical disk -> boot."
