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
    struct heap_block* prev;  // previous block, so kfree() need not search
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
    // The new stretch carries a block header of its own, so the space asked
    // for has to cover the header as well. Without that, a request for
    // exactly one grow step would be handed a block that is one header too
    // small and could never be satisfied.
    uint32_t want = align_up(bytes + sizeof(struct heap_block), HEAP_ALIGNMENT);
    uint32_t base;
    uint32_t offset = 0;
    struct heap_block* block;

    if (want < HEAP_GROW_BYTES) {
        want = HEAP_GROW_BYTES;
    }
    want = align_up(want, PMM_PAGE_SIZE);

    base = HEAP_BASE + heap_mapped;

    for (; offset < want; offset += PMM_PAGE_SIZE) {
        uint32_t frame = pmm_alloc_page();

        if (frame == 0) {
            break;
        }

        if (paging_map_page(base + offset, frame, PAGE_WRITABLE) != 0) {
            pmm_free_page(frame);
            break;
        }
    }

    if (offset < want) {
        // Undo the pages already mapped, so a failed grow costs nothing and
        // the next attempt starts where this one did.
        for (uint32_t at = 0; at < offset; at += PMM_PAGE_SIZE) {
            uint32_t frame = paging_unmap_page(base + at);

            if (frame != 0) {
                pmm_free_page(frame);
            }
        }

        return -1;
    }

    block = (struct heap_block*)(base);
    block->size = want - sizeof(struct heap_block);
    block->free = 1;
    block->next = 0;
    block->prev = 0;

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
        block->prev = tail;
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
            rest->prev = block;

            if (rest->next != 0) {
                rest->next->prev = rest;
            }

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
    // A size this large could not be rounded up, or have a block header
    // added to it in heap_grow(), without wrapping.
    if (size == 0 || size > 0x7FFF0000u) {
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

/* Is "block" really one of this heap's headers? The backward link answers
 * that without walking the list, which is what kept kfree() quadratic while a
 * tree, an environment or the whole file system was being torn down. Both
 * links are range-checked first, so a stray pointer cannot make this read
 * outside the mapped heap. */
static int block_linked(struct heap_block* block) {
    struct heap_block* previous = block->prev;
    struct heap_block* next = block->next;

    if (next != 0 &&
        ((uint32_t)next < HEAP_BASE || (uint32_t)next >= HEAP_BASE + heap_mapped ||
         ((uint32_t)next & (HEAP_ALIGNMENT - 1)) != 0)) {
        return 0;
    }

    if (previous == 0) {
        return block == first_block;
    }

    if ((uint32_t)previous < HEAP_BASE ||
        (uint32_t)previous >= HEAP_BASE + heap_mapped ||
        ((uint32_t)previous & (HEAP_ALIGNMENT - 1)) != 0) {
        return 0;
    }

    return previous->next == block;
}

/* Give an allocation back and merge it with free neighbours. */
void kfree(void* ptr) {
    if (ptr == 0) {
        return;
    }

    uint32_t address = (uint32_t)ptr;
    struct heap_block* block =
        (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));

    spinlock_acquire(&heap_lock);

    // Only a pointer this heap really handed out may be freed. A stray or
    // already-freed one would otherwise write its flag into whatever memory
    // it happens to point at, and corrupt the block list on the way out.
    if (address < HEAP_BASE || address >= HEAP_BASE + heap_mapped ||
        (address & (HEAP_ALIGNMENT - 1)) != 0 || !block_linked(block) ||
        block->free) {
        spinlock_release(&heap_lock);
        return;
    }

    block->free = 1;

    // Merge forward while the next block is free too.
    while (block->next != 0 && block->next->free) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;

        if (block->next != 0) {
            block->next->prev = block;
        }
    }

    // Then merge the block into a free predecessor, if there is one.
    struct heap_block* previous = block->prev;

    if (previous != 0 && previous->free) {
        previous->size += sizeof(struct heap_block) + block->size;
        previous->next = block->next;

        if (block->next != 0) {
            block->next->prev = previous;
        }
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
