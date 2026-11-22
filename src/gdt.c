#include "gdt.h"

#include <stdint.h>

// The Global Descriptor Table (GDT) is a table of eight-byte entries
// that the CPU reads whenever a segment register is loaded. Protected
// mode does not hand out flat addresses on its own: every memory access
// is computed as (segment selector) + (offset), and the selector is an
// index into the GDT saying where that segment starts and how far it
// reaches.
//
// AdaOS uses a "flat" model: each segment starts at address 0 and
// covers the whole 4 GB, so segment + offset is just the offset; a
// plain linear address. What really matters are the selectors, because
// everything else in the kernel hard-codes them:
//
//   0x00 = null entry (required by the CPU)
//   0x08 = kernel code, ring 0   <- where the ISR stubs run
//   0x10 = kernel data, ring 0   <- isr.asm reloads this one
//   0x18 = user code, ring 3     <- kept for when user mode arrives
//   0x20 = user data, ring 3
//
// GRUB handed us a GDT of its own, so after "lgdt" we must reload
// every segment register: CS included, which only a far jump can do.

/* One eight-byte GDT entry, exactly as the CPU lays it out. */
struct gdt_entry {
    /* bits 0-15 of the segment limit */
    uint16_t limit_low;
    /* bits 0-15 of the base address */
    uint16_t base_low;
    /* bits 16-23 of the base address */
    uint8_t base_middle;
    /* present bit, ring number, segment type */
    uint8_t access;
    /* limit bits 16-19 + flags */
    uint8_t granularity;
    /* bits 24-31 of the base address */
    uint8_t base_high;
} __attribute__((packed));

/* The pointer "lgdt" wants: a 16-bit limit followed by the base. */
struct gdt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define GDT_ACCESS_KERNEL_CODE 0x9A
#define GDT_ACCESS_KERNEL_DATA 0x92
#define GDT_ACCESS_USER_CODE 0xFA
#define GDT_ACCESS_USER_DATA 0xF2

/* Limit counts in 4 KB blocks, 32-bit: with limit 0xFFFFF the segment covers 4
 * GB. */
#define GDT_GRANULARITY_4KB_32BIT 0xCF

/* null, kernel code/data, user code/data */
#define GDT_ENTRY_COUNT 5

static struct gdt_entry gdt[GDT_ENTRY_COUNT];
static struct gdt_pointer gdt_pointer;

/* Fill in one entry as a flat segment covering all of memory. */
static void set_gate(int index, uint8_t access, uint8_t granularity) {
    gdt[index].base_low = 0;
    gdt[index].base_middle = 0;
    gdt[index].base_high = 0;
    gdt[index].limit_low = 0xFFFF;
    gdt[index].access = access;
    gdt[index].granularity = granularity;
}

/* Build the GDT and switch the CPU over to it. */
void gdt_init(void) {
    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_pointer.base = (uint32_t)&gdt[0];

    set_gate(0, 0, 0);  // the null entry, required
    set_gate(1, GDT_ACCESS_KERNEL_CODE, GDT_GRANULARITY_4KB_32BIT);
    set_gate(2, GDT_ACCESS_KERNEL_DATA, GDT_GRANULARITY_4KB_32BIT);
    set_gate(3, GDT_ACCESS_USER_CODE, GDT_GRANULARITY_4KB_32BIT);
    set_gate(4, GDT_ACCESS_USER_DATA, GDT_GRANULARITY_4KB_32BIT);

    // Load the table, then reload every segment register so the CPU
    // forgets GRUB's entries; the far jump is what reloads CS.
    __asm__ volatile(
        "lgdt %0\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:\n\t"
        :
        : "m"(gdt_pointer)
        : "eax", "memory");
}
