#include "paging.h"

#include <stdint.h>

#include "kprintf.h"
#include "libc.h"
#include "pmm.h"

// Paging: the layer that turns the addresses the kernel uses into the
// ones that reach real RAM. Protected mode without paging is a flat
// 4 GB; with paging, every access is translated through a two-level
// table walk:
//
//   CR3 -> page directory, 1024 entries of 4 KB regions
//          entry -> page table, 1024 entries of 4 KB pages
//                  entry -> the frame the page lives in
//
// AdaOS starts with an identity map: virtual address X points at
// physical X, just like before paging was on. Nothing has to move, the
// kernel keeps running at 1 MiB, and the VGA window at 0xB8000 is
// still where screen.c thinks it is. What paging buys is the ability
// to hand out addresses that do *not* exist in physical memory - which
// is exactly what the heap in kmalloc.c does with 0xC0000000.
//
// The identity map covers the first PAGING_IDENTITY_LIMIT bytes, or
// all usable RAM if the machine has less. Everything the kernel uses
// to *build* mappings - page tables, newly allocated frames - has to
// come from inside it, because the table walk itself needs the frame
// to be reachable. The physical allocator hands out low pages first,
// so that holds until a lot of memory is in use; when the heap and the
// drivers start needing more, raise the limit.

/* How much low memory the identity map covers: 64 MiB. */
#define PAGING_IDENTITY_LIMIT (64u * 1024u * 1024u)

/* CR0 bit 31: paging on. */
#define CR0_PAGING 0x80000000u

/* The page directory itself, 4 KiB aligned as CR3 requires. It is
 * static rather than allocated because it is needed to hand out the
 * first page, so it cannot come from the allocator. */
static uint32_t page_directory[1024] __attribute__((aligned(4096)));

static uint32_t identity_bytes = 0;
static struct paging_stats stats;

/* Read CR0 (control register 0). */
static uint32_t read_cr0(void) {
    uint32_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

/* Write CR0. */
static void write_cr0(uint32_t value) {
    __asm__ volatile("mov %0, %%cr0" : : "r"(value) : "memory");
}

/* The physical address of the page table covering "virt", allocating
 * one if "create" is set and there is none. 0 when there is none. */
static uint32_t* table_for(uint32_t virt, int create) {
    uint32_t index = virt >> 22; /* directory entries cover 4 MiB each */

    if ((page_directory[index] & PAGE_PRESENT) == 0) {
        if (!create) {
            return 0;
        }

        uint32_t phys = pmm_alloc_page();
        if (phys == 0) {
            return 0;
        }

        // A fresh table must be all zeroes: an absent entry is "no
        // mapping", and every other bit pattern would be a mapping.
        memset((void*)phys, 0, PMM_PAGE_SIZE);
        page_directory[index] = phys | PAGE_PRESENT | PAGE_WRITABLE;
        stats.page_tables++;
    }

    return (uint32_t*)(page_directory[index] & 0xFFFFF000u);
}

/* Point one virtual page at one physical page. */
int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t* table = table_for(virt, 1);

    if (table == 0) {
        return -1;
    }

    uint32_t* entry = &table[(virt >> 12) & 0x3FF];

    if ((*entry & PAGE_PRESENT) == 0) {
        stats.mapped_pages++;
    }

    *entry = (phys & 0xFFFFF000u) | PAGE_PRESENT | flags;
    return 0;
}

/* Identity-map low memory and turn paging on. */
void paging_init(void) {
    const struct pmm_stats* memory = pmm_get_stats();

    memset(page_directory, 0, sizeof(page_directory));
    stats.page_tables = 0;
    stats.mapped_pages = 0;

    uint32_t usable = memory->available_kb * 1024u;
    uint32_t limit =
        usable < PAGING_IDENTITY_LIMIT ? usable : PAGING_IDENTITY_LIMIT;
    limit &= 0xFFFFF000u;

    if (limit == 0) {
        // kprintf("Paging: no usable memory to map, staying unpaged\n");
        return;
    }

    for (uint32_t addr = 0; addr < limit; addr += PMM_PAGE_SIZE) {
        if (paging_map_page(addr, addr, PAGE_WRITABLE) != 0) {
            // Out of page tables: keep what we managed to map.
            limit = addr;
            break;
        }
    }

    identity_bytes = limit;
    stats.identity_bytes = limit;

    __asm__ volatile("mov %0, %%cr3"
                     :
                     : "r"((uint32_t)page_directory)
                     : "memory");
    write_cr0(read_cr0() | CR0_PAGING);

    // kprintf("Paging: identity map 0-%d KB, %d page tables\n",
    //        (int)(identity_bytes / 1024), (int)stats.page_tables);
}

/* What paging has mapped, for the "mem" command. */
const struct paging_stats* paging_get_stats(void) {
    stats.identity_bytes = identity_bytes;
    return &stats;
}
