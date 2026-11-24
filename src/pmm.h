#ifndef ADAOS_PMM_H
#define ADAOS_PMM_H

#include <stdint.h>

// The physical memory manager: one bitmap bit per 4 KiB frame, built once from
// the bootloader's memory map, where 1 means "in use" and 0 means "free". Every
// frame starts reserved and is only released when the map calls it available
// and the kernel does not occupy it. Allocation is a scan for the first zero
// bit, so page 0 is never handed out and 0 can safely mean failure.

/* Physical pages are 4 KiB: the size the x86 MMU works in. */
#define PMM_PAGE_SIZE 4096  // bytes per frame

/* What the allocator knows about the machine. */
struct pmm_stats {
    uint32_t total_kb;      // RAM the bootloader reported, holes included
    uint32_t available_kb;  // RAM the allocator can hand out
    uint32_t usable_end;  // one past the highest usable address, holes included
    uint32_t total_pages;  // available pages, i.e. the tracked ones
    uint32_t free_pages;
    uint32_t used_pages;
    uint32_t region_count;  // entries in the bootloader's memory map
    uint32_t kernel_start;  // where the kernel image sits in memory
    uint32_t kernel_end;
};

/* Read the bootloader's memory map and build the page bitmap. */
void pmm_init(uint32_t magic, uint32_t info_addr);

/* Reserve one free physical page; returns its address, or 0 when memory ran
 * out (page 0 is never handed out, so 0 is safe to mean "failed"). */
uint32_t pmm_alloc_page(void);

/* Give a page obtained from pmm_alloc_page() back. */
void pmm_free_page(uint32_t addr);

/* Never hand out a page at or above "bytes" again, which keeps every frame the
 * paging code uses inside the identity map it can reach. */
void pmm_set_alloc_ceiling(uint32_t bytes);

/* Current counters, for display. */
const struct pmm_stats* pmm_get_stats(void);

#endif /* ADAOS_PMM_H */
