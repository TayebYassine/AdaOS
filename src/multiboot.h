#ifndef ADAOS_MULTIBOOT_H
#define ADAOS_MULTIBOOT_H

#include <stdint.h>

// The Multiboot 1 structures: what GRUB hands the kernel in ebx at the
// moment it jumps to _start. Only the parts AdaOS uses are meaningful,
// but the whole multiboot_info layout is spelled out because every
// field after "flags" sits at a fixed offset - leaving one out would
// silently shift the rest.
//
// Reference: https://www.gnu.org/software/grub/manual/multiboot/

/* eax on entry: "yes, this really is a multiboot loader". */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* mem_lower/mem_upper are valid */
#define MULTIBOOT_INFO_MEMORY 0x00000001
/* mmap_length/mmap_addr are valid */
#define MULTIBOOT_INFO_MEM_MAP 0x00000040

// multiboot_mmap_entry.type values
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED 2

/* The first megabyte is never usable: BIOS data, the VGA window, option
 * ROMs. The map usually says so too. */
#define MULTIBOOT_LOW_MEMORY_END 0x00100000

/* The information structure itself. */
struct multiboot_info {
    uint32_t flags;
    /* KB of low memory (below 1 MiB) */
    uint32_t mem_lower;
    /* KB of memory above 1 MiB */
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    /* bytes of memory map that follow */
    uint32_t mmap_length;
    /* physical address */
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint32_t vbe_mode;
    uint32_t vbe_interface_seg;
    uint32_t vbe_interface_off;
    uint32_t vbe_interface_len;
} __attribute__((packed));

/* One entry of the memory map, tagged with what the region is for.
 *
 * The spec says addr and len are 64-bit, but they are kept here as two
 * 32-bit halves: that is the same bytes in memory (the machine is
 * little-endian), and it keeps the kernel free of 64-bit arithmetic,
 * which would otherwise make the linker pull in helper routines from
 * libgcc that a freestanding kernel does not link. */
struct multiboot_mmap_entry {
    /* size of this entry, not counting this field (20) */
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed));

#endif /* ADAOS_MULTIBOOT_H */
