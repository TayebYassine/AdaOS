#include "pmm.h"

#include <stdint.h>

#include "kprintf.h"
#include "libc.h"
#include "multiboot.h"

// The physical memory manager: it decides who gets which 4 KiB page of
// real RAM. It owns one bitmap with a bit per page - 1 means "in use",
// 0 means "free" - which is built once at boot from the memory map the
// bootloader hands over.
//
// The bitmap covers the first 4 GiB, the most this kernel can address
// (it runs in 32-bit mode without PAE), so 1,048,576 bits = 128 KiB of
// .bss. It starts out with every bit set: nothing is usable until the
// bootloader's map says so, and the map is not believed about anything
// the kernel itself occupies - the image, the map, the first megabyte.

/* Pages in the address space the bitmap covers (4 GiB / 4 KiB). */
#define PMM_MAX_PAGES (1024u * 1024u)
/* Bits in one bitmap word. */
#define PMM_BITS_PER_WORD 32u
/* Words in the bitmap. */
#define PMM_BITMAP_WORDS (PMM_MAX_PAGES / PMM_BITS_PER_WORD)

/* One bit per physical page: 1 = in use, 0 = free. */
static uint32_t bitmap[PMM_BITMAP_WORDS];

/* The highest page the bootloader mentioned, so every scan can stop
 * there instead of walking all 4 GiB. */
static uint32_t page_limit = 1;

/* Where the next allocation starts looking; without it every alloc
 * would rescan from the bottom of memory. */
static uint32_t search_hint = 1;

static struct pmm_stats stats;

// Boundary symbols from linker.ld, around the whole kernel image.
extern char kernel_start[];
extern char kernel_end[];

/* Is this page marked as in use? */
static int page_used(uint32_t page) {
    return (
        int)((bitmap[page / PMM_BITS_PER_WORD] >> (page % PMM_BITS_PER_WORD)) &
             1u);
}

/* Mark a page as in use. */
static void page_mark_used(uint32_t page) {
    bitmap[page / PMM_BITS_PER_WORD] |= 1u << (page % PMM_BITS_PER_WORD);
}

/* Mark a page as free. */
static void page_mark_free(uint32_t page) {
    bitmap[page / PMM_BITS_PER_WORD] &= ~(1u << (page % PMM_BITS_PER_WORD));
}

/* Turn the pages of a physical range into free ones, rounding the
 * range inwards to whole pages and ignoring anything below 1 MiB. */
static void free_region(uint32_t base, uint32_t length) {
    if (length == 0) {
        return;
    }

    // An end address that wrapped past 4 GiB means "to the top".
    uint32_t end = base + length;
    if (end < base) {
        end = 0xFFFFF000u;
    }

    uint32_t first = base / PMM_PAGE_SIZE;
    if (base % PMM_PAGE_SIZE != 0) {
        first++;
    }
    uint32_t last = end / PMM_PAGE_SIZE;

    uint32_t low_limit = MULTIBOOT_LOW_MEMORY_END / PMM_PAGE_SIZE;
    if (first < low_limit) {
        first = low_limit;
    }

    for (uint32_t page = first; page < last && page < PMM_MAX_PAGES; page++) {
        if (page_used(page)) {
            page_mark_free(page);
            stats.total_pages++;
        }

        if (page >= page_limit) {
            page_limit = page + 1;
        }
    }
}

/* Mark the pages of a physical range as in use, rounding outwards so a
 * partly used page is never handed out. */
static void reserve_region(uint32_t base, uint32_t length) {
    if (length == 0) {
        return;
    }

    uint32_t first = base / PMM_PAGE_SIZE;
    uint32_t end = base + length;
    if (end < base) {
        end = 0xFFFFF000u;
    }
    uint32_t last = end / PMM_PAGE_SIZE;
    if (end % PMM_PAGE_SIZE != 0) {
        last++;
    }

    for (uint32_t page = first; page < last && page < PMM_MAX_PAGES; page++) {
        page_mark_used(page);

        if (page >= page_limit) {
            page_limit = page + 1;
        }
    }
}

/* Find a free page in [from, to); 0 if there is none. */
static uint32_t scan_free_page(uint32_t from, uint32_t to) {
    for (uint32_t page = from; page < to && page < PMM_MAX_PAGES; page++) {
        if (!page_used(page)) {
            return page;
        }
    }

    return 0;
}

/* Read the memory map, free what is usable and keep the rest. */
void pmm_init(uint32_t magic, uint32_t info_addr) {
    memset(&stats, 0, sizeof(stats));
    memset(bitmap, 0xFF, sizeof(bitmap));

    page_limit = 1;
    search_hint = 1;

    stats.kernel_start = (uint32_t)kernel_start;
    stats.kernel_end = (uint32_t)kernel_end;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kprintf("Physical Memory: not booted by multiboot (magic 0x%x), no memory map\n",
                magic);
        return;
    }

    struct multiboot_info* info = (struct multiboot_info*)info_addr;

    if ((info->flags & MULTIBOOT_INFO_MEM_MAP) != 0) {
        uint32_t cursor = info->mmap_addr;
        uint32_t map_end = info->mmap_addr + info->mmap_length;

        while (cursor < map_end) {
            struct multiboot_mmap_entry* entry =
                (struct multiboot_mmap_entry*)cursor;

            stats.region_count++;

            if (entry->addr_high == 0) {
                stats.total_kb += entry->len_low >> 10;

                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    free_region(entry->addr_low, entry->len_low);
                }
            }

            // malformed map: stop rather than walk forever
            if (entry->size < 20) {
                break;
            }
            cursor += entry->size + sizeof(entry->size);
        }
    } else if ((info->flags & MULTIBOOT_INFO_MEMORY) != 0) {
        stats.region_count = 1;
        stats.total_kb = info->mem_upper + 1024;
        free_region(MULTIBOOT_LOW_MEMORY_END, info->mem_upper * 1024);
    } else {
        kprintf("Physical Memory: the bootloader gave no memory information\n");
        return;
    }

    reserve_region(0, MULTIBOOT_LOW_MEMORY_END);
    reserve_region(stats.kernel_start, stats.kernel_end - stats.kernel_start);
    reserve_region(info_addr, sizeof(*info));
    if ((info->flags & MULTIBOOT_INFO_MEM_MAP) != 0) {
        reserve_region(info->mmap_addr, info->mmap_length);
    }

    uint32_t free_pages = 0;
    for (uint32_t page = 0; page < page_limit; page++) {
        if (!page_used(page)) {
            free_pages++;
        }
    }

    stats.free_pages = free_pages;
    stats.used_pages = stats.total_pages - free_pages;
    stats.available_kb = stats.total_pages * (PMM_PAGE_SIZE / 1024);

    // kprintf("Physical Memory: %d KB usable, %d pages, %d free\n", (int)stats.available_kb,
    //        (int)stats.total_pages, (int)stats.free_pages);
}

/* Reserve one free page; 0 means out of memory. Page 0 is never
 * returned, so 0 can safely mean "failed". */
uint32_t pmm_alloc_page(void) {
    if (stats.free_pages == 0) {
        return 0;
    }

    uint32_t page = scan_free_page(search_hint, page_limit);

    if (page == 0) {
        page = scan_free_page(1, search_hint);
    }

    if (page == 0) {
        return 0;
    }

    page_mark_used(page);
    stats.free_pages--;
    stats.used_pages++;
    search_hint = page + 1;

    return page * PMM_PAGE_SIZE;
}

/* Release a page taken from pmm_alloc_page(). */
void pmm_free_page(uint32_t addr) {
    if (addr % PMM_PAGE_SIZE != 0 || addr < MULTIBOOT_LOW_MEMORY_END) {
        return;
    }

    uint32_t page = addr / PMM_PAGE_SIZE;

    if (page >= page_limit || !page_used(page)) {
        return;
    }

    page_mark_free(page);
    stats.used_pages--;
    stats.free_pages++;

    if (page < search_hint) {
        search_hint = page;
    }
}

/* Counters for the "mem" command. */
const struct pmm_stats* pmm_get_stats(void) { return &stats; }
