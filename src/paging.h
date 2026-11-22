#ifndef ADAOS_PAGING_H
#define ADAOS_PAGING_H

#include <stdint.h>

/* Bits in a page-directory or page-table entry. */
#define PAGE_PRESENT 0x1
#define PAGE_WRITABLE 0x2
#define PAGE_USER 0x4

/* What the paging layer has set up, for display. */
struct paging_stats {
    /* how much of low memory is identity mapped */
    uint32_t identity_bytes;
    /* tables claimed from the physical allocator */
    uint32_t page_tables;
    /* pages with a present entry somewhere */
    uint32_t mapped_pages;
};

/* Build the page directory, identity-map low memory and switch the CPU
 * over to it. Call after pmm_init(), before anything maps new memory. */
void paging_init(void);

/* Point the virtual address "virt" at the physical page "phys", adding
 * a page table if that region has none yet. flags is a PAGE_* mask
 * (PAGE_PRESENT is implied). Returns 0 on success. */
int paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/* What paging_init() and later mappings have done, for display. */
const struct paging_stats* paging_get_stats(void);

#endif /* ADAOS_PAGING_H */
