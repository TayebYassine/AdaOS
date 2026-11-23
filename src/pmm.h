#ifndef ADAOS_PMM_H
#define ADAOS_PMM_H

#include <stdint.h>

/* Physical pages are 4 KiB: the size the x86 MMU works in. */
#define PMM_PAGE_SIZE 4096

/* What the allocator knows about the machine. */
struct pmm_stats {
    /* RAM the bootloader reported, holes included */
    uint32_t total_kb;
    /* RAM the allocator can hand out */
    uint32_t available_kb;
    /* one past the highest usable address: the top of memory, holes
     * included, as opposed to available_kb, which only counts pages */
    uint32_t usable_end;
    /* available pages, i.e. the tracked ones */
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t used_pages;
    /* entries in the bootloader's memory map */
    uint32_t region_count;
    /* where the kernel image sits in memory */
    uint32_t kernel_start;
    uint32_t kernel_end;
};

/* Read the bootloader's memory map and build the page bitmap. Pass the
 * magic and information address kernel_main() received. */
void pmm_init(uint32_t magic, uint32_t info_addr);

/* Reserve one free physical page; returns its address, or 0 if the
 * memory ran out (page 0 is never handed out, so 0 is safe to mean
 * "failed"). */
uint32_t pmm_alloc_page(void);

/* Give a page obtained from pmm_alloc_page() back. */
void pmm_free_page(uint32_t addr);

/* Never hand out a page at or above "bytes" again. paging.c uses it to
 * keep every frame inside the identity map, so a frame it hands back is
 * always reachable by the kernel while building a new mapping. */
void pmm_set_alloc_ceiling(uint32_t bytes);

/* Current counters, for display. */
const struct pmm_stats* pmm_get_stats(void);

#endif /* ADAOS_PMM_H */
