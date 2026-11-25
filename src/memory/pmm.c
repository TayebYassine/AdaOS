#include "pmm.h"

#include <stdint.h>

#include "kprintf.h"
#include "libc.h"
#include "multiboot.h"

/* Pages in the address space the bitmap covers (4 GiB / 4 KiB). */
#define PMM_MAX_PAGES (1024u * 1024u)
/* Bits in one bitmap word. */
#define PMM_BITS_PER_WORD 32u
/* Words in the bitmap. */
#define PMM_BITMAP_WORDS (PMM_MAX_PAGES / PMM_BITS_PER_WORD)

// The bitmap covers 4 GiB in 32-bit mode without PAE: 128 KiB of .bss that
// starts with every page taken, because nothing is usable until the map
// says so.
/* One bit per physical page: 1 = in use, 0 = free. */
static uint32_t bitmap[PMM_BITMAP_WORDS];

/* The highest page the bootloader mentioned, so scans stop there. */
static uint32_t page_limit = 1;

/* Where the next allocation starts looking, so it need not rescan from 0. */
static uint32_t search_hint = 1;

/* First page that is off limits (see pmm_set_alloc_ceiling()). */
static uint32_t ceiling_page = PMM_MAX_PAGES;

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

/* Free the whole pages of a physical range, ignoring anything below 1 MiB. */
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

    if (end > stats.usable_end) {
        stats.usable_end = end;
    }

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

/* Mark the pages of a physical range, rounded outwards so no page is shared. */
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

/* Find a free page in [from, to); 0 if there is none. A whole word of taken
 * pages is skipped at once, which is what a nearly full or fragmented memory
 * turns into: one bitmap read per 32 pages instead of one per page. */
static uint32_t scan_free_page(uint32_t from, uint32_t to) {
    uint32_t page = from;

    while (page < to && page < PMM_MAX_PAGES) {
        uint32_t word = page / PMM_BITS_PER_WORD;
        uint32_t bits = bitmap[word];

        if (bits != 0xFFFFFFFFu) {
            // Ignore the bits before "page" in this word, then take the
            // lowest free one left (__builtin_ctz finds it in one step).
            bits |= (1u << (page % PMM_BITS_PER_WORD)) - 1u;

            if (bits != 0xFFFFFFFFu) {
                uint32_t found = word * PMM_BITS_PER_WORD +
                                 (uint32_t)__builtin_ctz(~bits);

                return found < to ? found : 0;
            }
        }

        page = (word + 1) * PMM_BITS_PER_WORD;
    }

    return 0;
}

/* Read the memory map, free what is usable and keep the rest. */
void pmm_init(uint32_t magic, uint32_t info_addr) {
    memset(&stats, 0, sizeof(stats));
    memset(bitmap, 0xFF, sizeof(bitmap));

    page_limit = 1;
    search_hint = 1;
    ceiling_page = PMM_MAX_PAGES;

    stats.kernel_start = (uint32_t)kernel_start;
    stats.kernel_end = (uint32_t)kernel_end;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kprintf(
            "Physical Memory: not booted by multiboot (magic 0x%x), no memory "
            "map\n",
            magic);
        return;
    }

    struct multiboot_info* info = (struct multiboot_info*)info_addr;

    // Free every region the map calls available; anything else stays taken.
    if ((info->flags & MULTIBOOT_INFO_MEM_MAP) != 0) {
        uint32_t cursor = info->mmap_addr;
        uint32_t map_end = info->mmap_addr + info->mmap_length;

        // A length that wraps past 4 GiB describes a map that cannot exist;
        // the per-entry checks below still keep the walk inside memory.
        if (map_end < cursor) {
            map_end = 0xFFFFFFFFu;
        }

        while (cursor < map_end) {
            struct multiboot_mmap_entry* entry =
                (struct multiboot_mmap_entry*)cursor;
            uint32_t remaining = map_end - cursor;

            // Each entry is a 4-byte size field plus at least 20 bytes, and
            // must lie inside the map that was described. Anything else is a
            // malformed map, so stop rather than read past its end.
            if (entry->size < 20 || remaining < 24 ||
                entry->size > remaining - 4) {
                break;
            }

            stats.region_count++;

            if (entry->addr_high == 0) {
                stats.total_kb += entry->len_low >> 10;

                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    free_region(entry->addr_low, entry->len_low);
                }
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

    // Whatever the map claims, the kernel's own pieces are never handed out.
    reserve_region(0, MULTIBOOT_LOW_MEMORY_END);
    reserve_region(stats.kernel_start, stats.kernel_end - stats.kernel_start);
    reserve_region(info_addr, sizeof(*info));
    if ((info->flags & MULTIBOOT_INFO_MEM_MAP) != 0) {
        reserve_region(info->mmap_addr, info->mmap_length);
    }

    if ((info->flags & MULTIBOOT_INFO_MODS) != 0 && info->mods_count > 0) {
        struct multiboot_module* mods =
            (struct multiboot_module*)info->mods_addr;

        reserve_region(info->mods_addr,
                       info->mods_count * sizeof(struct multiboot_module));

        for (uint32_t i = 0; i < info->mods_count; i++) {
            reserve_region(mods[i].mod_start,
                           mods[i].mod_end - mods[i].mod_start);
        }
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
}

/* Reserve one free page; 0 means out of memory. Page 0 is never returned. */
uint32_t pmm_alloc_page(void) {
    if (stats.free_pages == 0) {
        return 0;
    }

    uint32_t scan_end = page_limit < ceiling_page ? page_limit : ceiling_page;
    uint32_t page = scan_free_page(search_hint, scan_end);

    // Nothing above the hint: look again from the bottom, up to the hint.
    if (page == 0) {
        uint32_t wrap_end = search_hint < scan_end ? search_hint : scan_end;

        page = scan_free_page(1, wrap_end);
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

/* Refuse pages from "bytes" upwards: they sit outside the paging map. */
void pmm_set_alloc_ceiling(uint32_t bytes) {
    uint32_t page = bytes / PMM_PAGE_SIZE;

    ceiling_page = page < PMM_MAX_PAGES ? page : PMM_MAX_PAGES;
}

/* Counters for the "mem" command. */
const struct pmm_stats* pmm_get_stats(void) { return &stats; }
