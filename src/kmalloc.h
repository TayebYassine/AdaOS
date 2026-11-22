#ifndef ADAOS_KMALLOC_H
#define ADAOS_KMALLOC_H

#include <stddef.h>
#include <stdint.h>

/* How the heap is doing, for display. */
struct heap_stats {
    /* virtual space handed to the heap so far */
    uint32_t mapped_bytes;
    /* inside live blocks */
    uint32_t used_bytes;
    /* inside free blocks */
    uint32_t free_bytes;
    uint32_t block_count;
};

/* Prepare the heap. Call after paging_init(). */
void kmalloc_init(void);

/* Allocate "size" bytes; 0 when the heap cannot grow any further. */
void* kmalloc(size_t size);

/* Give an allocation back. Freeing 0 is allowed and does nothing. */
void kfree(void* ptr);

/* Totals for the "mem" command. */
const struct heap_stats* kmalloc_get_stats(void);

#endif /* ADAOS_KMALLOC_H */
