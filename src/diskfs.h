#ifndef ADAOS_DISKFS_H
#define ADAOS_DISKFS_H

#include <stdint.h>

/* Look at the first disk: is there one, and does it hold a file system?
 * Returns 1 when a usable disk answered; the answer is remembered, so
 * calling this again costs nothing. */
int diskfs_probe(void);

/* Is there a disk, but no file system on it yet? This is what makes a
 * boot the first one ever. */
int diskfs_blank(void);

/* Write an empty file system over whatever the disk held. With "files"
 * set the tree in memory goes onto the disk too; without it the tree is
 * left alone, unsaved. "progress", when not 0, is called with the
 * percentage done. Returns 1 on success. */
int diskfs_format_disk(int files, void (*progress)(int percent));

/* Write the whole tree out, reporting progress; returns 1 on success. */
int diskfs_write_tree(void (*progress)(int percent));

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
