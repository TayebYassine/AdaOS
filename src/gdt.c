#include "gdt.h"

#include <stdint.h>

/* One eight-byte GDT entry, exactly as the CPU lays it out. */
struct gdt_entry {
    uint16_t limit_low;   // bits 0-15 of the segment limit
    uint16_t base_low;    // bits 0-15 of the base address
    uint8_t base_middle;  // bits 16-23 of the base address
    uint8_t access;       // present bit, ring number, segment type
    uint8_t granularity;  // limit bits 16-19, then the flags
    uint8_t base_high;    // bits 24-31 of the base address
} __attribute__((packed));

/* The pointer "lgdt" wants: a 16-bit limit followed by the base. */
struct gdt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Access byte: present, ring 0/3, code or data, readable/writable.
#define GDT_ACCESS_KERNEL_CODE 0x9A  // present, ring 0, code, readable
#define GDT_ACCESS_KERNEL_DATA 0x92  // present, ring 0, data, writable
#define GDT_ACCESS_USER_CODE 0xFA    // present, ring 3, code, readable
#define GDT_ACCESS_USER_DATA 0xF2    // present, ring 3, data, writable

/* Limit counts in 4 KB blocks, 32-bit: limit 0xFFFFF covers all 4 GB. */
#define GDT_GRANULARITY_4KB_32BIT 0xCF  // 4 KB granularity, 32-bit segment

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

    set_gate(0, 0, 0);  // the null entry, required by the CPU
    set_gate(1, GDT_ACCESS_KERNEL_CODE, GDT_GRANULARITY_4KB_32BIT);
    set_gate(2, GDT_ACCESS_KERNEL_DATA, GDT_GRANULARITY_4KB_32BIT);
    set_gate(3, GDT_ACCESS_USER_CODE, GDT_GRANULARITY_4KB_32BIT);
    set_gate(4, GDT_ACCESS_USER_DATA, GDT_GRANULARITY_4KB_32BIT);

    // Load the table, then reload every segment register; only the far
    // jump can reload CS, which still points into the loader's table.
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
