#include "kmalloc.h"

#include <stdint.h>

#include "libc.h"
#include "paging.h"
#include "pmm.h"
#include "spinlock.h"

/* Where the heap starts in virtual space, clear of the image at 1 MiB. */
#define HEAP_BASE 0xC0000000u

/* How much virtual space one grow step adds. */
#define HEAP_GROW_BYTES (16u * 1024u)

/* A remainder smaller than this is not worth its own header. */
#define HEAP_MIN_SPLIT 16u

/* Payloads are handed out 8-byte aligned. */
#define HEAP_ALIGNMENT 8u

// The header is padded to 16 bytes, which keeps the payload behind it
// 8-byte aligned as long as the block itself is.
/* Every block starts with this. */
struct heap_block {
    uint32_t size;            // bytes of payload, not counting this header
    uint32_t free;            // 1 while the block is available
    struct heap_block* next;  // next block in address order
    uint32_t padding;         // keeps the header 16 bytes
};

/* Guards every field below, the block list above all. */
static struct spinlock heap_lock;

/* Head of the block list, in address order. */
static struct heap_block* first_block = 0;

/* How much virtual space the heap owns so far. */
static uint32_t heap_mapped = 0;

static struct heap_stats stats;

/* Round "value" up to a multiple of "alignment" (a power of two). */
static uint32_t align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/* Start with an empty heap; the first kmalloc() maps the first pages. */
void kmalloc_init(void) {
    first_block = 0;
    heap_mapped = 0;
}

/* Add fresh space to the heap and make it one free block; 0 on success. */
static int heap_grow(uint32_t bytes) {
    uint32_t want = align_up(bytes, HEAP_ALIGNMENT);

    if (want < HEAP_GROW_BYTES) {
        want = HEAP_GROW_BYTES;
    }
    want = align_up(want, PMM_PAGE_SIZE);

    uint32_t base = HEAP_BASE + heap_mapped;

    for (uint32_t offset = 0; offset < want; offset += PMM_PAGE_SIZE) {
        uint32_t frame = pmm_alloc_page();

        if (frame == 0) {
            return -1;
        }

        if (paging_map_page(base + offset, frame, PAGE_WRITABLE) != 0) {
            pmm_free_page(frame);
            return -1;
        }
    }

    struct heap_block* block = (struct heap_block*)(base);
    block->size = want - sizeof(struct heap_block);
    block->free = 1;
    block->next = 0;
    block->padding = 0;

    heap_mapped += want;

    if (first_block == 0) {
        first_block = block;
        return 0;
    }

    // The new stretch sits right after the last block, so it either joins
    // that block when it is free or becomes the new tail.
    struct heap_block* tail = first_block;
    while (tail->next != 0) {
        tail = tail->next;
    }

    if (tail->free) {
        tail->size += sizeof(struct heap_block) + block->size;
    } else {
        tail->next = block;
    }

    return 0;
}

/* First fit: cut a block of "need" bytes out of the list, or 0. */
static void* take_block(uint32_t need) {
    for (struct heap_block* block = first_block; block != 0;
         block = block->next) {
        if (!block->free || block->size < need) {
            continue;
        }

        if (block->size >= need + sizeof(struct heap_block) + HEAP_MIN_SPLIT) {
            struct heap_block* rest =
                (struct heap_block*)((uint8_t*)block +
                                     sizeof(struct heap_block) + need);
            rest->size = block->size - need - sizeof(struct heap_block);
            rest->free = 1;
            rest->next = block->next;
            rest->padding = 0;

            block->next = rest;
            block->size = need;
        }

        block->free = 0;
        return (void*)((uint8_t*)block + sizeof(struct heap_block));
    }

    return 0;
}

/* Allocate "size" bytes, or 0 when the heap cannot grow. */
void* kmalloc(size_t size) {
    if (size == 0) {
        return 0;
    }

    uint32_t need = align_up((uint32_t)size, HEAP_ALIGNMENT);
    void* result = 0;

    spinlock_acquire(&heap_lock);

    // Two attempts: the first may fail only because there is no block big
    // enough, and growing the heap then makes room for a second try.
    for (int attempt = 0; attempt < 2; attempt++) {
        result = take_block(need);

        if (result != 0) {
            break;
        }

        if (heap_grow(need) != 0) {
            break;
        }
    }

    spinlock_release(&heap_lock);

    return result;
}

/* Give an allocation back and merge it with free neighbours. */
void kfree(void* ptr) {
    if (ptr == 0) {
        return;
    }

    struct heap_block* block =
        (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));

    spinlock_acquire(&heap_lock);

    block->free = 1;

    // Merge forward while the next block is free too.
    while (block->next != 0 && block->next->free) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
    }

    // Then merge the block into a free predecessor, if there is one.
    struct heap_block* previous = first_block;
    while (previous != 0 && previous->next != block) {
        previous = previous->next;
    }

    if (previous != 0 && previous->free) {
        previous->size += sizeof(struct heap_block) + block->size;
        previous->next = block->next;
    }

    spinlock_release(&heap_lock);
}

/* Totals for the "mem" command. */
const struct heap_stats* kmalloc_get_stats(void) {
    uint32_t used = 0;
    uint32_t free_bytes = 0;
    uint32_t blocks = 0;

    spinlock_acquire(&heap_lock);

    for (struct heap_block* block = first_block; block != 0;
         block = block->next) {
        blocks++;

        if (block->free) {
            free_bytes += block->size;
        } else {
            used += block->size;
        }
    }

    stats.mapped_bytes = heap_mapped;
    stats.used_bytes = used;
    stats.free_bytes = free_bytes;
    stats.block_count = blocks;

    spinlock_release(&heap_lock);

    return &stats;
}
