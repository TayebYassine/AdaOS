#include "paging.h"

#include <stdint.h>

#include "kprintf.h"
#include "libc.h"
#include "pmm.h"
#include "screen.h"

/* How much low memory the identity map covers: 64 MiB. */
#define PAGING_IDENTITY_LIMIT (64u * 1024u * 1024u)

/* CR0 bit 31: paging on. */
#define CR0_PAGING 0x80000000u

/* The page directory itself, 4 KiB aligned as CR3 requires. It is static
 * because the very first mapping needs it, before the heap exists. */
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

/* The page table covering "virt", created when "create" is set; 0 when none. */
static uint32_t* table_for(uint32_t virt, int create) {
    uint32_t index = virt >> 22;  // each directory entry covers 4 MiB

    if ((page_directory[index] & PAGE_PRESENT) == 0) {
        if (!create) {
            return 0;
        }

        uint32_t phys = pmm_alloc_page();
        if (phys == 0) {
            return 0;
        }

        // A fresh table must be all zeroes: an absent entry means "no
        // mapping", and any other bit pattern would map something.
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

/* Drop the mapping for one virtual page; returns the frame it used, or 0. */
uint32_t paging_unmap_page(uint32_t virt) {
    uint32_t* table = table_for(virt, 0);
    uint32_t* entry;
    uint32_t frame;

    if (table == 0) {
        return 0;
    }

    entry = &table[(virt >> 12) & 0x3FF];

    if ((*entry & PAGE_PRESENT) == 0) {
        return 0;
    }

    frame = *entry & 0xFFFFF000u;
    *entry = 0;

    // The CPU may still hold the old translation in its TLB.
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");

    if (stats.mapped_pages > 0) {
        stats.mapped_pages--;
    }

    return frame;
}

/* Identity-map low memory and turn paging on. */
void paging_init(void) {
    const struct pmm_stats* memory = pmm_get_stats();

    memset(page_directory, 0, sizeof(page_directory));
    stats.page_tables = 0;
    stats.mapped_pages = 0;

    // The map is bounded by the top of usable memory as well as by the
    // limit: a frame above usable_end could be a hole, and mapping it
    // would fault as soon as the kernel touched it.
    uint32_t limit = memory->usable_end < PAGING_IDENTITY_LIMIT
                         ? memory->usable_end
                         : PAGING_IDENTITY_LIMIT;
    limit &= 0xFFFFF000u;

    if (limit == 0) {
        // Nothing usable was reported, so there is no memory to map and no
        // memory to run on. Carrying on would leave paging off while the
        // heap believed its pages were mapped, so stop here instead of
        // corrupting whatever the first allocation touched.
        kprintf("AdaOS: the bootloader reported no usable memory.\n");
        print("System halted.\n");

        for (;;) {
            __asm__ volatile("hlt");
        }
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

    // Nothing above the map is reachable, so the page allocator must not
    // hand out frames from up there either.
    pmm_set_alloc_ceiling(limit);

    __asm__ volatile("mov %0, %%cr3"
                     :
                     : "r"((uint32_t)page_directory)
                     : "memory");
    write_cr0(read_cr0() | CR0_PAGING);
}

/* What paging has mapped, for the "mem" command. */
const struct paging_stats* paging_get_stats(void) {
    stats.identity_bytes = identity_bytes;
    return &stats;
}
