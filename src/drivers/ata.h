#ifndef ADAOS_ATA_H
#define ADAOS_ATA_H

#include <stdint.h>

// ATA disk access over the primary IDE bus, PIO mode, polling. A disk is a
// stack of 512-byte sectors addressed by LBA; a command names the first sector
// and a count, and the CPU then moves 16-bit words through the data port. Only
// one disk is looked for, and every wait is bounded so a missing or wedged
// drive fails instead of hanging.

/* The unit an ATA disk reads and writes in. */
#define ATA_SECTOR_SIZE 512  // bytes in one LBA sector

/* Find the first disk on the primary bus; 1 if one answered. */
int ata_init(void);

/* Did a disk answer at boot? */
int ata_present(void);

/* Sectors the disk has, 0 when there is none. */
uint32_t ata_sector_count(void);

/* The model name the drive reported. */
const char* ata_model(void);

/* Read "count" sectors starting at "lba" into "buffer"; 1 on success. */
int ata_read(uint32_t lba, void* buffer, uint32_t count);

/* Write "count" sectors from "buffer" starting at "lba"; 1 on success.
 * The drive's write cache is not flushed: batch writes and call
 * ata_flush_cache() once when the run is done. */
int ata_write(uint32_t lba, const void* buffer, uint32_t count);

/* Make the drive write its cache out to the disk; 1 on success. */
int ata_flush_cache(void);

#endif /* ADAOS_ATA_H */
