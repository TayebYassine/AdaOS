#!/usr/bin/env bash
# AdaOS - build the kernel and run it on QEMU.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"

mkdir -p "$BUILD"

echo "[1/4] Assembling .asm sources..."
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

echo "[2/4] Compiling C sources..."
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
    # -fno-builtin: we define strlen/memcpy/... ourselves in libc.c
    # -fno-asynchronous-unwind-tables: no .eh_frame, nothing unwinds in a kernel
    gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -DADAOS_VERSION="\"$VERSION\"" \
        "${INCLUDES[@]}" -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done < <(find "$SRC" -type f -name '*.c' | sort)

echo "[3/4] Linking kernel.elf..."
ld -m elf_i386 -T "$SRC/boot/linker.ld" \
    -o "$BUILD/kernel.elf" "${OBJECTS[@]}"

echo "[4/4] Booting on QEMU..."
# QEMU loads the starting files itself and passes them as a module.
tar -C "$ROOT/files" -cf "$BUILD/init.tar" .

# Any extra arguments you pass to build.sh are forwarded to QEMU,
# throws the old disk away first, so the machine boots on an empty,
# freshly created file system.
NEW_DISK=0
QEMU_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --new-disk) NEW_DISK=1 ;;
        *)          QEMU_ARGS+=("$arg") ;;
    esac
done

# The disk the file system lives on: 16 MiB, made blank on the first run
# and kept afterwards, so files survive a reboot. Delete it to start over,
# or pass --new-disk to this script.
DISK="$BUILD/disk.img"
if [ "$NEW_DISK" -eq 1 ]; then
    echo "  --new-disk: deleting $DISK"
    rm -f "$DISK"
fi
if [ ! -f "$DISK" ]; then
    dd if=/dev/zero of="$DISK" bs=1M count=16 status=none
fi

exec qemu-system-i386 -kernel "$BUILD/kernel.elf" -initrd "$BUILD/init.tar" \
    -drive file="$DISK",format=raw,if=ide \
    ${QEMU_ARGS[@]+"${QEMU_ARGS[@]}"}
