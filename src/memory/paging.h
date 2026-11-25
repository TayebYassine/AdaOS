#ifndef ADAOS_PAGING_H
#define ADAOS_PAGING_H

#include <stdint.h>

// Virtual memory: a two-level table walk (page directory -> page table ->
// frame) that turns the addresses code uses into physical pages. Low memory is
// identity mapped at boot so the kernel keeps running where it was, and further
// virtual pages can be pointed at any frame afterwards - which is what lets the
// heap occupy scattered physical memory. Frames used to build mappings must
// themselves be reachable through the map.

/* Bits in a page-directory or page-table entry. */
#define PAGE_PRESENT 0x1   // the entry maps something
#define PAGE_WRITABLE 0x2  // writes are allowed through it
#define PAGE_USER 0x4      // ring 3 may reach it

/* What the paging layer has set up, for display. */
struct paging_stats {
    uint32_t identity_bytes;  // how much of low memory is identity mapped
    uint32_t page_tables;     // tables claimed from the physical allocator
    uint32_t mapped_pages;    // pages with a present entry somewhere
};

/* Build the page directory, identity-map low memory and switch the CPU over
 * to it. Call after pmm_init(), before anything maps new memory. */
void paging_init(void);

/* Point "virt" at the physical page "phys", adding a page table when the
 * region has none. flags is a PAGE_* mask; PAGE_PRESENT is implied. */
int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/* Drop the mapping for one virtual page and flush the cached translation.
 * Returns the frame it used to point at, or 0 when nothing was mapped there,
 * so the caller can give that frame back to the allocator. */
uint32_t paging_unmap_page(uint32_t virt);

/* What paging_init() and later mappings have done, for display. */
const struct paging_stats* paging_get_stats(void);

#endif /* ADAOS_PAGING_H */
