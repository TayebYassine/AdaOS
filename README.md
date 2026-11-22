<p align="center">
  <img src="./ada.jpeg" alt="AdaOS logo" width="128" style="border-radius: 15px;" />
</p>

<h1 align="center">AdaOS</h1>

AdaOS is a tiny command-line operating system for 32-bit x86, written in C and Assembly. It boots through GRUB and drops you into a DOS-style shell.

> [!CAUTION]
> AdaOS is a hobby project, not an official product. It's experimental and may have bugs.
> Run it in a virtual machine such as [VirtualBox](https://www.virtualbox.org/) or [QEMU](https://www.qemu.org/) rather than on real hardware.

## Minimum requirements

AdaOS boots from the ISO through a multiboot loader and runs entirely from RAM.

| Component  | Minimum                            | Notes |
|------------|------------------------------------|-------|
| CPU        | 32-bit x86 (i386 or newer)         | Runs in 32-bit protected mode; x86-64 CPUs work in their legacy 32-bit mode |
| Firmware   | Legacy BIOS (or CSM)               | **No UEFI support** — an EFI-only machine will not boot the ISO |
| RAM        | 32 MB                              | The kernel is ~31 KB loaded at 1 MiB with a 16 KB stack; there is no memory manager yet, so extra RAM is unused |
| Boot media | CD/DVD drive, or USB stick         | `adaos.iso` is a hybrid image: writing it to a USB stick with `dd` also boots on BIOS machines |
| Display    | VGA-compatible adapter, text mode  | Text mode only (80x25), no graphics mode and no graphics driver |
| Keyboard   | PS/2 keyboard                      | Scancode set 1 via IRQ1; USB keyboards only work where the firmware or VM emulates PS/2 |
| Storage    | none                               | No disk driver and no filesystem: nothing is written or persisted |

AdaOS has been tested with QEMU (`qemu-system-i386`) and VirtualBox 7.2. In VirtualBox use the `Other` (32-bit) type, BIOS firmware (not EFI), an IDE controller and 32 MB of RAM or more.

## Commands

| Command    | Description                 |
|------------|-----------------------------|
| `help`     | List available commands     |
| `print`    | Print text to the screen    |
| `clear`    | Clear the screen            |
| `reboot`   | Restart the machine         |
| `shutdown` | Power off the machine       |
| `uptime`   | Show how long AdaOS has been running |
| `sleep`    | Pause for a number of seconds |

## Building

Requirements: `gcc` (with 32-bit support), `nasm`, `ld`, `grub-mkrescue`, `xorriso`, and QEMU or VirtualBox.

```bash
./build-iso.sh
qemu-system-i386 -cdrom adaos.iso
```

