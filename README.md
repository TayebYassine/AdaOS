<p align="center">
  <img src="./ada.jpeg" alt="AdaOS logo" width="128" style="border-radius: 15px;" />
</p>

<h1 align="center">AdaOS</h1>

AdaOS is a tiny command-line operating system for 32-bit x86, written in C and Assembly. It boots through GRUB and drops you into a DOS-style shell.

> [!NOTE]
> AdaOS is a hobby project, not an official product. It's experimental and may have bugs.
> Run it in a virtual machine such as [VirtualBox](https://www.virtualbox.org/) or [QEMU](https://www.qemu.org/) rather than on real hardware.

## Commands

| Command    | Description                 |
|------------|-----------------------------|
| `help`     | List available commands     |
| `print`    | Print text to the screen    |
| `clear`    | Clear the screen            |
| `reboot`   | Restart the machine         |
| `shutdown` | Power off the machine       |

## Building

Requirements: `gcc` (with 32-bit support), `nasm`, `ld`, `grub-mkrescue`, `xorriso`, and QEMU or VirtualBox.

```bash
./build-iso.sh
qemu-system-i386 -cdrom adaos.iso
```

