#include "kmalloc.h"

#include <stdint.h>

#include "libc.h"
#include "paging.h"
#include "pmm.h"

// The kernel heap: kmalloc() and kfree(), so the kernel no longer has
// to size its data structures at compile time (the shell's fixed
// history, the keyboard's fixed buffer, ...).
//
// The heap lives at HEAP_BASE in *virtual* space and grows upwards in
// 4 KB steps. Each new page is a frame from the physical allocator,
// mapped in by paging.c - so the heap occupies whatever physical pages
// are free, not one contiguous stretch of RAM, which is the point of
// having paging at all.
//
// Inside that growing region the allocator is the classic first-fit
// free list: every block carries a header with its payload size and
// whether it is free, blocks are split when an allocation would leave
// a useful remainder behind, and kfree() merges neighbours so small
// allocations do not fragment the heap permanently.

/* Where the heap starts in virtual space: high up, well clear of the
 * kernel image at 1 MiB and of anything the identity map covers. */
#define HEAP_BASE 0xC0000000u

/* How much virtual space one grow step adds. */
#define HEAP_GROW_BYTES (16u * 1024u)

/* A remainder smaller than this is not worth its own header: keep it
 * attached to the block instead of splitting. */
#define HEAP_MIN_SPLIT 16u

/* Payloads are handed out 8-byte aligned. */
#define HEAP_ALIGNMENT 8u

/* Every block starts with this. The padding field exists only to make
 * the header 16 bytes, which keeps the payload behind it 8-byte
 * aligned as long as the block itself is. */
struct heap_block {
    /* bytes of payload, not counting this header */
    uint32_t size;
    uint32_t free;
    struct heap_block* next;
    uint32_t padding;
};

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

/* Add "bytes" of fresh space to the heap: claim frames, map them, and
 * make the new stretch one free block. Returns 0 on success. */
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

/* Allocate "size" bytes, or 0 when the heap cannot grow. */
void* kmalloc(size_t size) {
    if (size == 0) {
        return 0;
    }

    uint32_t need = align_up((uint32_t)size, HEAP_ALIGNMENT);

    for (int attempt = 0; attempt < 2; attempt++) {
        for (struct heap_block* block = first_block; block != 0;
             block = block->next) {
            if (!block->free || block->size < need) {
                continue;
            }

            if (block->size >=
                need + sizeof(struct heap_block) + HEAP_MIN_SPLIT) {
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

        if (heap_grow(need) != 0) {
            break;
        }
    }

    return 0;
}

/* Give an allocation back and merge it with free neighbours. */
void kfree(void* ptr) {
    if (ptr == 0) {
        return;
    }

    struct heap_block* block =
        (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));

    block->free = 1;

    while (block->next != 0 && block->next->free) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
    }

    struct heap_block* previous = first_block;
    while (previous != 0 && previous->next != block) {
        previous = previous->next;
    }

    if (previous != 0 && previous->free) {
        previous->size += sizeof(struct heap_block) + block->size;
        previous->next = block->next;
    }
}

/* Totals for the "mem" command. */
const struct heap_stats* kmalloc_get_stats(void) {
    uint32_t used = 0;
    uint32_t free_bytes = 0;
    uint32_t blocks = 0;

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

    return &stats;
}
