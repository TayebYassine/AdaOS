#ifndef ADAOS_ATA_H
#define ADAOS_ATA_H

#include <stdint.h>

/* The unit an ATA disk reads and writes in. */
#define ATA_SECTOR_SIZE 512

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

/* Write "count" sectors from "buffer" starting at "lba"; 1 on success. */
int ata_write(uint32_t lba, const void* buffer, uint32_t count);

#endif /* ADAOS_ATA_H */
