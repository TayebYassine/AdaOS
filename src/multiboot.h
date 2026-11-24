#ifndef ADAOS_MULTIBOOT_H
#define ADAOS_MULTIBOOT_H

#include <stdint.h>

// The Multiboot 1 hand-off structures: what the bootloader passes in ebx when
// it jumps to the kernel entry point. Only some fields are used, but the whole
// layout is spelled out because every field after "flags" sits at a fixed
// offset - leaving one out would silently shift the rest. The memory map
// arrives as a chain of entries, each tagged available or reserved.
//
// Reference: https://www.gnu.org/software/grub/manual/multiboot/

/* eax on entry: "yes, this really is a multiboot loader". */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* mem_lower/mem_upper are valid */
#define MULTIBOOT_INFO_MEMORY 0x00000001
/* mods_count/mods_addr are valid */
#define MULTIBOOT_INFO_MODS 0x00000008
/* mmap_length/mmap_addr are valid */
#define MULTIBOOT_INFO_MEM_MAP 0x00000040

// multiboot_mmap_entry.type values
#define MULTIBOOT_MEMORY_AVAILABLE 1  // RAM free for the kernel to use
#define MULTIBOOT_MEMORY_RESERVED 2   // ROM, firmware or device memory

/* The first megabyte is never usable: BIOS data, the VGA window, option ROMs.
 */
#define MULTIBOOT_LOW_MEMORY_END 0x00100000

/* One module the loader put in memory; in this kernel, the initial ramdisk. */
struct multiboot_module {
    uint32_t mod_start;  // first byte
    uint32_t mod_end;    // one past the last
    uint32_t string;     // a string the loader attached, not used here
    uint32_t reserved;
} __attribute__((packed));

/* The information structure itself. */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;  // KB of low memory (below 1 MiB)
    uint32_t mem_upper;  // KB of memory above 1 MiB
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;  // bytes of memory map that follow
    uint32_t mmap_addr;    // physical address
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

/* One memory map entry. addr and len are 64-bit in the spec, kept here as two
 * 32-bit halves: same bytes little-endian, and no 64-bit arithmetic to link. */
struct multiboot_mmap_entry {
    uint32_t size;  // size of this entry, not counting this field (20)
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed));

#endif /* ADAOS_MULTIBOOT_H */
