#ifndef ADAOS_DISKFS_H
#define ADAOS_DISKFS_H

#include <stdint.h>

/* Mount the file system on the first disk; a blank disk is formatted.
 * Returns 1 when the tree is backed by the disk. */
int diskfs_init(void);

/* Write the tree out if it changed since the last save. */
int diskfs_sync(void);

/* Is a disk behind the file system? */
int diskfs_present(void);

/* Sectors on the disk, and sectors holding file data. */
uint32_t diskfs_total_sectors(void);
uint32_t diskfs_used_sectors(void);

/* Entries stored now, and entries that fit in the table. */
uint32_t diskfs_entry_count(void);
uint32_t diskfs_entry_capacity(void);

#endif /* ADAOS_DISKFS_H */
