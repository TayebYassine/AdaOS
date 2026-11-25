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
# Assemble every .asm file in src/ into a matching .o in build/, keeping
# the module directories they live in.
OBJECTS=()
while IFS= read -r src; do
    rel="${src#"$SRC"/}"
    obj="$BUILD/${rel%.asm}.o"
    mkdir -p "$(dirname "$obj")"
    nasm -f elf32 "$src" -o "$obj"
    OBJECTS+=("$obj")
done < <(find "$SRC" -type f -name '*.asm' | sort)
# The version the screen shows, stamped with the date of this build
VERSION="AdaOS-$(date +%y.%m.%d)"

# Every module directory is on the include path, so headers are included by
# their bare names.
INCLUDES=()
while IFS= read -r dir; do
    INCLUDES+=("-I$dir")
done < <(find "$SRC" -type d | sort)

# Compile every .c file in src/ into a matching .o in build/
while IFS= read -r src; do
    rel="${src#"$SRC"/}"
    obj="$BUILD/${rel%.c}.o"
    mkdir -p "$(dirname "$obj")"
    # -O2: without it every file was built at -O0, the single biggest cost in
    #       the whole system, from the scheduler to the disk writes.
    # -mno-sse -mno-mmx: an interrupt handler must not use the FPU or MMX
    #       state it never saves; x86-64 would otherwise vectorize freely.
    # -Wall -Wextra: the kernel is freestanding, so the compiler is the only
    #       reviewer looking for suspicious conversions and unused results.
    # -fno-builtin: we define strlen/memcpy/... ourselves in libc.c, and this
    #       also stops GCC from turning a loop into a call to one of them
    # -fno-asynchronous-unwind-tables: no .eh_frame, nothing unwinds in a kernel
    gcc -m32 -O2 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -mno-sse -mno-mmx -Wall -Wextra \
        -DADAOS_VERSION="\"$VERSION\"" \
        "${INCLUDES[@]}" -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done < <(find "$SRC" -type f -name '*.c' | sort)
ld -m elf_i386 -T "$SRC/boot/linker.ld" \
    -o "$BUILD/kernel.elf" "${OBJECTS[@]}"

echo "[2/4] Staging ISO tree..."
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub"
cp "$BUILD/kernel.elf" "$ISO_DIR/boot/"

# Pack the starting files as the module GRUB hands the kernel.
tar -C "$ROOT/files" -cf "$ISO_DIR/boot/init.tar" .

echo "[3/4] Writing GRUB config..."
cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=0
set default=0

menuentry "AdaOS" {
    multiboot /boot/kernel.elf
    module /boot/init.tar
    boot
}
EOF

echo "[4/4] Creating ISO with grub-mkrescue..."
grub-mkrescue -o "$ISO_OUT" "$ISO_DIR"

echo "Done: $ISO_OUT"
echo "In VirtualBox: create a VM -> Storage -> add this ISO as an optical disk -> boot."
echo "Add a hard disk to the VM as well, or the file system stays in RAM."
