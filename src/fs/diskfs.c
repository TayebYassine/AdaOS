#include "diskfs.h"

#include <stdint.h>

#include "ata.h"
#include "fs.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "libc.h"

#define DISKFS_MAGIC "ADAOSFS1"  // eight bytes, no terminator on disk
#define DISKFS_VERSION 1

/* Entries the table holds, and the longest path one can name. */
#define DISKFS_ENTRY_CAPACITY 128
#define DISKFS_PATH_MAX 112

/* Bits in an entry's flags. */
#define DISKFS_USED 0x01  // the slot describes something
#define DISKFS_DIR 0x02   // it is a directory, not a file

/* Everything about the disk, kept in sector 0. */
struct diskfs_super {
    char magic[8];
    uint32_t version;
    uint32_t sectors;
    uint32_t entry_start;     // first sector of the table
    uint32_t entry_sectors;   // sectors the table occupies
    uint32_t data_start;      // first sector of the file data
    uint32_t entry_count;     // entries in use
    uint32_t entry_capacity;  // entries the table can hold
    uint8_t pad[476];
} __attribute__((packed));

/* One file or directory on the disk. */
struct diskfs_entry {
    char path[DISKFS_PATH_MAX];  // absolute path, for files and directories
    uint32_t size;               // bytes of file data
    uint32_t sector;             // first sector of the data
    uint32_t flags;              // DISKFS_USED | DISKFS_DIR
    uint32_t pad;
} __attribute__((packed));

#define DISKFS_TABLE_BYTES (DISKFS_ENTRY_CAPACITY * sizeof(struct diskfs_entry))
#define DISKFS_TABLE_SECTORS \
    ((DISKFS_TABLE_BYTES + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE)
#define DISKFS_DATA_START (1 + DISKFS_TABLE_SECTORS)

/* Room for the table and at least a few data sectors. */
#define DISKFS_MIN_SECTORS (DISKFS_DATA_START + 8)

static struct diskfs_super super;
static struct diskfs_entry table[DISKFS_ENTRY_CAPACITY];

// The node each table slot was planned from, in the same order as table[].
// write_data() would otherwise look every path up again: a string walk and a
// lock per entry, for a tree the planning pass had already walked.
static struct fs_node* planned_nodes[DISKFS_ENTRY_CAPACITY];

static uint8_t sector_buffer[ATA_SECTOR_SIZE];

static int present = 0;
static int probed = 0;
static int blank = 0;

// Cleared when the disk holds a file system that could not be read. Nothing
// may be written over it then: whatever is there is still the user's data, and
// the in-memory tree stays in RAM until the disk is formatted on purpose.
static int writable = 0;

// Set when setup was told to only try AdaOS out: the disk, blank or not, is
// left exactly as it is and this session works in RAM.
static int ram_only = 0;
static uint32_t saved_entries = 0;
static uint32_t used_sectors = 0;
static uint32_t planned = 0;

// Progress is reported as a percentage of the phase currently running: the
// hook is handed progress_base plus a share of progress_span.
static void (*progress_hook)(int percent) = 0;
static int progress_base = 0;
static int progress_span = 100;
static uint32_t progress_done = 0;
static uint32_t progress_total = 0;

/* Tell the watcher, if there is one, how far the phase has got. */
static void report_progress(int percent) {
    if (progress_hook != 0) {
        progress_hook(percent);
    }
}

/* The percentage the save has earned so far. */
static int save_percent(void) {
    if (progress_total == 0) {
        return progress_base + progress_span;
    }

    return progress_base +
           (int)(progress_done * (uint32_t)progress_span / progress_total);
}

/* Does the superblock describe this disk and our layout? */
static int super_load(void) {
    if (!ata_read(0, &super, 1)) {
        return 0;
    }

    return strncmp(super.magic, DISKFS_MAGIC, sizeof(super.magic)) == 0 &&
           super.version == DISKFS_VERSION &&
           super.sectors >= DISKFS_MIN_SECTORS &&
           super.sectors <= ata_sector_count() && super.entry_start == 1 &&
           super.entry_sectors == DISKFS_TABLE_SECTORS &&
           super.data_start == DISKFS_DATA_START &&
           super.entry_capacity == DISKFS_ENTRY_CAPACITY &&
           super.entry_count <= DISKFS_ENTRY_CAPACITY;
}

/* Write the superblock describing the table and data as they are now. */
static int super_save(void) {
    memset(&super, 0, sizeof(super));
    memcpy(super.magic, DISKFS_MAGIC, sizeof(super.magic));
    super.version = DISKFS_VERSION;
    super.sectors = ata_sector_count();
    super.entry_start = 1;
    super.entry_sectors = DISKFS_TABLE_SECTORS;
    super.data_start = DISKFS_DATA_START;
    super.entry_count = saved_entries;
    super.entry_capacity = DISKFS_ENTRY_CAPACITY;

    memset(sector_buffer, 0, sizeof(sector_buffer));
    memcpy(sector_buffer, &super, sizeof(super));

    return ata_write(0, sector_buffer, 1);
}

/* Give every node under "dir" an entry, and its files a place to go. */
static int plan_tree(struct fs_node* dir, const char* path, uint32_t len,
                     uint32_t* next_sector) {
    for (struct fs_node* child = dir->children; child != 0;
         child = child->sibling) {
        char child_path[FS_PATH_MAX];
        uint32_t name_len = (uint32_t)strlen(child->name);
        uint32_t child_len = len;
        struct diskfs_entry* entry;

        if (child_len + 1 + name_len + 1 > FS_PATH_MAX) {
            kprintf("Error: the path of %s is too long.\n", child->name);
            return 0;
        }

        memcpy(child_path, path, len);
        if (child_path[len - 1] != '/') {
            child_path[child_len++] = '/';
        }
        memcpy(child_path + child_len, child->name, name_len + 1);
        child_len += name_len;

        if (child_len >= DISKFS_PATH_MAX) {
            kprintf(
                "Error: the path of %s does not fit the %d bytes an entry "
                "holds; nothing was saved.\n",
                child->name, DISKFS_PATH_MAX - 1);
            return 0;
        }

        if (planned == DISKFS_ENTRY_CAPACITY) {
            kprintf(
                "Error: the disk file table holds %d entries and is full; "
                "nothing was saved.\n",
                DISKFS_ENTRY_CAPACITY);
            return 0;
        }

        entry = &table[planned];
        planned_nodes[planned] = child;
        planned++;
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->path, child_path, child_len + 1);
        entry->flags = DISKFS_USED;

        if (child->kind == FS_DIR) {
            entry->flags |= DISKFS_DIR;

            if (!plan_tree(child, child_path, child_len, next_sector)) {
                return 0;
            }
        } else {
            uint32_t sectors =
                (child->size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

            entry->size = child->size;
            entry->sector = *next_sector;
            *next_sector += sectors;
        }
    }

    return 1;
}

/* Write one entry's content. A file's sectors were planned back to back, so
 * the whole run goes out in one command (ata_write splits it into 256-sector
 * chunks itself); only the partial last sector needs a padded copy. */
static int write_data(const struct diskfs_entry* entry,
                      const struct fs_node* node) {
    uint32_t full = entry->size / ATA_SECTOR_SIZE;
    uint32_t rest = entry->size % ATA_SECTOR_SIZE;

    if (node == 0 || node->kind != FS_FILE || node->size != entry->size ||
        node->data == 0) {
        return 0;
    }

    if (full != 0) {
        if (!ata_write(entry->sector, node->data, full)) {
            return 0;
        }

        progress_done += full;
        report_progress(save_percent());
    }

    if (rest != 0) {
        // The rest of the sector is padding, and must not be leftover bytes
        // from whichever file used this buffer last.
        memset(sector_buffer, 0, sizeof(sector_buffer));
        memcpy(sector_buffer, node->data + full * ATA_SECTOR_SIZE, rest);

        if (!ata_write(entry->sector + full, sector_buffer, 1)) {
            return 0;
        }

        progress_done++;
        report_progress(save_percent());
    }

    return 1;
}

/* Write the whole tree out: data first, then the table, superblock last. */
static int diskfs_save(void) {
    uint32_t next = DISKFS_DATA_START;
    uint32_t table_sectors;

    memset(table, 0, sizeof(table));
    memset(planned_nodes, 0, sizeof(planned_nodes));
    planned = 0;

    if (!plan_tree(fs_root(), "/", 1, &next)) {
        return 0;
    }

    progress_total = next - DISKFS_DATA_START;
    progress_done = 0;
    report_progress(progress_base);

    if (next > ata_sector_count()) {
        kprintf("Error: the disk is full.\n");
        return 0;
    }

    for (uint32_t i = 0; i < planned; i++) {
        if ((table[i].flags & DISKFS_DIR) != 0 || table[i].size == 0) {
            continue;
        }
        if (!write_data(&table[i], planned_nodes[i])) {
            return 0;
        }
    }

    // Only the sectors the entries fill need writing. The table reserves room
    // for 128 entries, so a tree of a few small files was paying for 30
    // sectors of padding on every single save. The entry count in the
    // superblock keeps the loader from reading the stale rest.
    table_sectors = (uint32_t)(((size_t)planned * sizeof(*table) +
                               ATA_SECTOR_SIZE - 1) /
                              ATA_SECTOR_SIZE);

    if (table_sectors == 0) {
        table_sectors = 1;
    }

    if (!ata_write(1, table, table_sectors)) {
        return 0;
    }

    // Everything above is in the drive's cache. Push it out before the
    // superblock, which is the commit point: once its entry count is on the
    // platters the file data it names must be there too.
    if (!ata_flush_cache()) {
        return 0;
    }

    saved_entries = planned;
    used_sectors = next - DISKFS_DATA_START;

    return super_save() && ata_flush_cache();
}

/* Put one entry back into the tree. */
static int load_entry(const struct diskfs_entry* entry) {
    char path[DISKFS_PATH_MAX];
    uint32_t sectors;
    char* data;

    memcpy(path, entry->path, DISKFS_PATH_MAX);
    path[DISKFS_PATH_MAX - 1] = '\0';

    // A path that is not absolute would land somewhere unexpected.
    if (path[0] != '/') {
        return 0;
    }

    if ((entry->flags & DISKFS_DIR) != 0) {
        return fs_mkdir(path) != 0;
    }

    if (entry->size == 0) {
        return fs_write(path, "", 0) != 0;
    }

    // The table comes off the disk, so its numbers cannot be trusted: a size
    // that would overflow the sector sum, or a span that does not lie inside
    // the drive, is refused before anything is allocated.
    if (entry->size > 0xFFFFFFFFu - (ATA_SECTOR_SIZE - 1)) {
        return 0;
    }

    sectors = (entry->size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (entry->sector < DISKFS_DATA_START ||
        entry->sector >= ata_sector_count() ||
        sectors > ata_sector_count() - entry->sector) {
        return 0;
    }

    // ata_read() moves whole sectors, so the buffer must hold the padding at
    // the end of the last one too; a buffer of entry->size + 1 let that last,
    // partial sector run into the next heap block.
    data = (char*)kmalloc((size_t)sectors * ATA_SECTOR_SIZE + 1);
    if (data == 0) {
        return 0;
    }

    if (!ata_read(entry->sector, data, sectors)) {
        kfree(data);
        return 0;
    }

    data[entry->size] = '\0';

    // The tree takes the buffer over: copying it again would double both the
    // work and the memory each file needs while it loads.
    if (fs_write_owned(path, data, entry->size) == 0) {
        kfree(data);
        return 0;
    }

    return 1;
}

/* Replace the tree with what the disk holds. */
static int diskfs_load(void) {
    uint32_t entries = super.entry_count;
    uint32_t used = 0;

    if (!ata_read(1, table, DISKFS_TABLE_SECTORS)) {
        return 0;
    }

    fs_reset();

    for (uint32_t i = 0; i < entries; i++) {
        if ((table[i].flags & DISKFS_USED) == 0) {
            continue;
        }
        if (!load_entry(&table[i])) {
            kprintf("Warning: a disk entry could not be read.\n");
            return 0;
        }

        if ((table[i].flags & DISKFS_DIR) == 0) {
            uint32_t sectors =
                (table[i].size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

            if (table[i].sector + sectors - DISKFS_DATA_START > used) {
                used = table[i].sector + sectors - DISKFS_DATA_START;
            }
        }
    }

    fs_mark_clean();
    saved_entries = entries;
    used_sectors = used;

    return 1;
}

/* Write a table with no entries in it. */
static int write_empty_table(void) {
    memset(table, 0, sizeof(table));

    return ata_write(1, table, DISKFS_TABLE_SECTORS);
}

/* Lay down an empty table and superblock. */
static int diskfs_format(void) {
    saved_entries = 0;
    used_sectors = 0;
    planned = 0;

    if (!write_empty_table() || !ata_flush_cache()) {
        return 0;
    }

    report_progress(9);

    if (!super_save() || !ata_flush_cache()) {
        return 0;
    }

    report_progress(10);

    return 1;
}

/* Look at the first disk and remember whether it holds a file system. */
int diskfs_probe(void) {
    if (probed) {
        return present;
    }

    probed = 1;
    present = 0;
    blank = 0;

    if (!ata_init()) {
        kprintf(
            "Disk: none on the primary IDE bus; the file system is in RAM.\n");
        return 0;
    }

    if (ata_sector_count() < DISKFS_MIN_SECTORS) {
        kprintf("Disk: too small to hold a file system.\n");
        return 0;
    }

    present = 1;
    blank = !super_load();

    return 1;
}

/* Is there a disk, but no file system on it yet? */
int diskfs_blank(void) { return present && blank; }

/* Format the disk, and write the memory tree out when "files" is set. */
int diskfs_format_disk(int files, void (*progress)(int percent)) {
    if (!diskfs_probe()) {
        return 0;
    }

    progress_hook = progress;
    progress_base = 0;
    progress_span = 100;
    progress_done = 0;
    progress_total = 0;

    report_progress(0);

    if (!diskfs_format()) {
        kprintf("Error: the disk cannot be written.\n");
        present = 0;
        progress_hook = 0;
        return 0;
    }

    // The disk holds a usable file system again, so writing to it is allowed.
    writable = 1;

    if (files) {
        // The table is written; the file data takes the rest of the bar.
        progress_base = 10;
        progress_span = 89;

        if (!diskfs_save()) {
            progress_base = 0;
            progress_span = 100;
            progress_hook = 0;
            return 0;
        }

        fs_mark_clean();
    } else {
        report_progress(99);
    }

    blank = 0;
    report_progress(100);

    progress_base = 0;
    progress_span = 100;
    progress_hook = 0;

    return 1;
}

/* Write the whole tree out, reporting progress. */
int diskfs_write_tree(void (*progress)(int percent)) {
    if (!present) {
        return 0;
    }
    if (!writable) {
        kprintf(
            "Error: the disk file system is not readable; nothing was "
            "written over it.\n");
        return 0;
    }

    progress_hook = progress;
    progress_base = 0;
    progress_span = 99;
    progress_done = 0;
    progress_total = 0;

    report_progress(0);

    if (!diskfs_save()) {
        progress_hook = 0;
        return 0;
    }

    fs_mark_clean();
    blank = 0;
    report_progress(100);

    progress_base = 0;
    progress_span = 100;
    progress_hook = 0;

    return 1;
}

/* Keep this session in RAM and never write to the disk. */
void diskfs_use_ram(void) { ram_only = 1; }

int diskfs_ram_only(void) { return ram_only; }

/* Mount the disk file system; a blank disk is formatted and given the tree. */
int diskfs_init(void) {
    if (ram_only) {
        return 0;
    }

    if (!diskfs_probe()) {
        return 0;
    }

    if (!blank) {
        if (diskfs_load()) {
            writable = 1;
            return 1;
        }

        // The disk holds something that could not be read back. Formatting it
        // here would throw the user's files away for the sake of a clean boot,
        // so the disk is left exactly as it is and this session works in RAM.
        kprintf(
            "Warning: the disk file system could not be read. It was left "
            "untouched and this session will not write to it.\n");

        return 0;
    }

    if (!diskfs_format_disk(1, 0)) {
        kprintf("Error: the file system could not be written.\n");
        return 0;
    }

    return 1;
}

/* Write the tree out if it changed since the last save. */
int diskfs_sync(void) {
    if (!present) {
        return 0;
    }
    if (ram_only) {
        return 1;
    }
    if (!fs_dirty()) {
        return 1;
    }
    if (!writable) {
        kprintf(
            "Error: the disk file system could not be read, so nothing was "
            "written over it. The changes are in RAM only.\n");
        return 0;
    }

    if (!diskfs_save()) {
        kprintf("Error: the disk could not be written.\n");
        return 0;
    }

    fs_mark_clean();

    return 1;
}

/* Is a disk behind the file system? */
int diskfs_present(void) { return present; }

uint32_t diskfs_total_sectors(void) { return ata_sector_count(); }

uint32_t diskfs_used_sectors(void) { return used_sectors; }

uint32_t diskfs_entry_count(void) { return saved_entries; }

uint32_t diskfs_entry_capacity(void) { return DISKFS_ENTRY_CAPACITY; }
