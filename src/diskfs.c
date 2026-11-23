#include "diskfs.h"

#include <stdint.h>

#include "ata.h"
#include "fs.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "libc.h"

// The disk file system: the tree in fs.c written out to the first ATA
// disk, and read back at boot.
//
// Layout, with one sector = 512 bytes:
//
//   sector 0        the superblock: magic, version, sizes, entry count
//   sectors 1..32   the entry table: 128 entries of 128 bytes each
//   sectors 33..    file data, back to back, in table order
//
// An entry is a full path plus the file's size and first sector, so the
// disk needs no directory blocks: a path is its own address, and the
// table holds the whole namespace at once. Directories get entries too,
// which keeps empty ones around.
//
// Saving rewrites everything. The tree is small; writing it whole means
// no free list, no fragmentation, and no partial updates to reason
// about.

#define DISKFS_MAGIC "ADAOSFS1"
#define DISKFS_VERSION 1

/* Entries the table holds, and the longest path one can name. */
#define DISKFS_ENTRY_CAPACITY 128
#define DISKFS_PATH_MAX 112

/* Bits in an entry's flags. */
#define DISKFS_USED 0x01
#define DISKFS_DIR 0x02

/* Everything about the disk in one sector. */
struct diskfs_super {
    char magic[8];
    uint32_t version;
    uint32_t sectors;
    uint32_t entry_start;
    uint32_t entry_sectors;
    uint32_t data_start;
    uint32_t entry_count;
    uint32_t entry_capacity;
    uint8_t pad[476];
} __attribute__((packed));

/* One file or directory on the disk. */
struct diskfs_entry {
    char path[DISKFS_PATH_MAX];
    uint32_t size;
    uint32_t sector;
    uint32_t flags;
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
static uint8_t sector_buffer[ATA_SECTOR_SIZE];

static int present = 0;
static uint32_t saved_entries = 0;
static uint32_t used_sectors = 0;
static uint32_t planned = 0;

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
            kprintf("Error: the path of %s is too long.\n", child->name);
            return 0;
        }

        if (planned == DISKFS_ENTRY_CAPACITY) {
            kprintf("Error: the disk file table is full.\n");
            return 0;
        }

        entry = &table[planned++];
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

/* Write one entry's content, a sector at a time. */
static int write_data(const struct diskfs_entry* entry) {
    struct fs_node* node = fs_lookup(entry->path);
    uint32_t done = 0;

    if (node == 0 || node->kind != FS_FILE || node->size != entry->size) {
        return 0;
    }

    while (done < entry->size) {
        uint32_t n = entry->size - done;

        if (n > ATA_SECTOR_SIZE) {
            n = ATA_SECTOR_SIZE;
        }

        memset(sector_buffer, 0, sizeof(sector_buffer));
        memcpy(sector_buffer, node->data + done, n);

        if (!ata_write(entry->sector + done / ATA_SECTOR_SIZE, sector_buffer,
                       1)) {
            return 0;
        }

        done += n;
    }

    return 1;
}

/* Write the whole tree out: data first, then the table, superblock last. */
static int diskfs_save(void) {
    uint32_t next = DISKFS_DATA_START;

    memset(table, 0, sizeof(table));
    planned = 0;

    if (!plan_tree(fs_root(), "/", 1, &next)) {
        return 0;
    }
    if (next > ata_sector_count()) {
        kprintf("Error: the disk is full.\n");
        return 0;
    }

    for (uint32_t i = 0; i < planned; i++) {
        if ((table[i].flags & DISKFS_DIR) != 0 || table[i].size == 0) {
            continue;
        }
        if (!write_data(&table[i])) {
            return 0;
        }
    }

    if (!ata_write(1, table, DISKFS_TABLE_SECTORS)) {
        return 0;
    }

    saved_entries = planned;
    used_sectors = next - DISKFS_DATA_START;

    return super_save();
}

/* Put one entry back into the tree. */
static int load_entry(const struct diskfs_entry* entry) {
    char path[DISKFS_PATH_MAX];
    uint32_t sectors;
    char* data;

    memcpy(path, entry->path, DISKFS_PATH_MAX);
    path[DISKFS_PATH_MAX - 1] = '\0';

    if (path[0] != '/') {
        return 0;
    }

    if ((entry->flags & DISKFS_DIR) != 0) {
        return fs_mkdir(path) != 0;
    }

    if (entry->size == 0) {
        return fs_write(path, "", 0) != 0;
    }

    sectors = (entry->size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    if (entry->sector < DISKFS_DATA_START ||
        entry->sector + sectors > ata_sector_count()) {
        return 0;
    }

    data = (char*)kmalloc(entry->size + 1);
    if (data == 0) {
        return 0;
    }

    if (!ata_read(entry->sector, data, sectors)) {
        kfree(data);
        return 0;
    }

    data[entry->size] = '\0';

    if (fs_write(path, data, entry->size) == 0) {
        kfree(data);
        return 0;
    }

    kfree(data);
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

/* Lay down an empty table and superblock. */
static void diskfs_format(void) {
    memset(table, 0, sizeof(table));
    saved_entries = 0;
    used_sectors = 0;

    if (!ata_write(1, table, DISKFS_TABLE_SECTORS) || !super_save()) {
        kprintf("Error: the disk cannot be written.\n");
        present = 0;
    }
}

int diskfs_init(void) {
    present = 0;

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

    if (super_load()) {
        if (diskfs_load()) {
            // kprintf("Disk: %s, %d MB, %d entries restored\n", ata_model(),
            //        (int)(ata_sector_count() / 2048), (int)saved_entries);
            return 1;
        }

        kprintf("Warning: the disk file system is damaged; rebuilding it.\n");
        fs_reset();
    }

    diskfs_format();
    if (!present) {
        return 0;
    }

    if (!diskfs_save()) {
        kprintf("Error: the file system could not be written.\n");
        return 0;
    }

    fs_mark_clean();
    
    //kprintf("Disk: %s, %d MB, file system created\n", ata_model(),
    //        (int)(ata_sector_count() / 2048));

    return 1;
}

int diskfs_sync(void) {
    if (!present) {
        return 0;
    }
    if (!fs_dirty()) {
        return 1;
    }

    if (!diskfs_save()) {
        kprintf("Error: the disk could not be written.\n");
        return 0;
    }

    fs_mark_clean();

    return 1;
}

int diskfs_present(void) { return present; }

uint32_t diskfs_total_sectors(void) { return ata_sector_count(); }

uint32_t diskfs_used_sectors(void) { return used_sectors; }

uint32_t diskfs_entry_count(void) { return saved_entries; }

uint32_t diskfs_entry_capacity(void) { return DISKFS_ENTRY_CAPACITY; }
