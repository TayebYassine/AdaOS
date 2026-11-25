#ifndef ADAOS_DISKFS_H
#define ADAOS_DISKFS_H

#include <stdint.h>

// The on-disk file system: the in-memory tree written out to the first ATA
// disk and read back at boot. The layout, one sector = 512 bytes:
//
//   sector 0        superblock: magic, version, sizes, entry count
//   sectors 1..32   entry table: 128 entries of 128 bytes each
//   sectors 33..    file data, back to back, in table order
//
// An entry is a full path plus the file's size and first sector, so no
// directory blocks are needed: a path is its own address and the table holds
// the whole namespace at once. Directories get entries too, which keeps empty
// ones around. Saving rewrites everything, which keeps the format free of free
// lists, fragmentation and partial updates.

/* Look at the first disk and remember whether it holds a file system. */
int diskfs_probe(void);

/* Is there a disk, but no file system on it yet? */
int diskfs_blank(void);

/* Write an empty file system over the disk; with "files" the memory tree goes
 * out too. "progress" may be 0, otherwise it gets the percentage done. */
int diskfs_format_disk(int files, void (*progress)(int percent));

/* Write the whole tree out, reporting progress; returns 1 on success. */
int diskfs_write_tree(void (*progress)(int percent));

/* Mount the file system on the first disk; a blank disk is formatted. */
int diskfs_init(void);

/* Write the tree out if it changed since the last save. */
int diskfs_sync(void);

/* Is a disk behind the file system? */
int diskfs_present(void);

uint32_t diskfs_total_sectors(void);   // sectors on the disk
uint32_t diskfs_used_sectors(void);    // sectors holding file data
uint32_t diskfs_entry_count(void);     // entries stored now
uint32_t diskfs_entry_capacity(void);  // entries the table can hold

#endif /* ADAOS_DISKFS_H */
