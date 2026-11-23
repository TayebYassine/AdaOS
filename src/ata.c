#include "ata.h"

#include <stdint.h>

#include "io.h"
#include "libc.h"

// The ATA driver: the primary IDE bus, PIO mode, polling.
//
// A disk is a stack of 512-byte sectors addressed by number (LBA). The
// controller has a handful of ports: a command port, a status port, and
// a 16-bit data port that moves one word per read or write. A command
// names the starting sector and how many sectors follow; the drive then
// raises DRQ once per sector, and the CPU copies 256 words through the
// data port. Nothing here uses interrupts: the drive is asked to stay
// quiet (nIEN) and we wait on its status bits instead, so the driver
// works before the IDT is set up.
//
// Every wait has a timeout, so a missing or wedged drive fails with 0
// instead of hanging the kernel.

// Ports of the primary IDE bus
#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_CONTROL 0x3F6

// Status bits
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_DRDY 0x40
#define ATA_STATUS_BSY 0x80

// Commands
#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC

/* Master drive: bit 6 of the head register asks for sector addressing, so
 * the cylinder and head bits are replaced by the top LBA bits. */
#define ATA_DRIVE_LBA 0xE0
/* The same drive with the head register left in its plain form. */
#define ATA_DRIVE_MASTER 0xA0
/* Set nIEN: the drive must not raise IRQ14 while we poll it. */
#define ATA_CONTROL_NIEN 0x02

/* Sector count register: 0 means 256 sectors. */
#define ATA_MAX_CHUNK 256

/* Port reads to wait for; a few seconds at worst. */
#define ATA_TIMEOUT 2000000

static int present = 0;
static uint32_t sectors = 0;
static char model[41];

/* Reading the status port four times is the architected 400 ns wait. */
static void ata_delay(void) {
    for (int i = 0; i < 4; i++) {
        (void)inb(ATA_STATUS);
    }
}

/* Wait until the drive is not busy; 0 on timeout or a reported error. */
static int ata_wait_idle(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);

        if ((status & ATA_STATUS_BSY) != 0) {
            continue;
        }

        return (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) == 0;
    }

    return 0;
}

/* Wait until the drive wants a sector through the data port. */
static int ata_wait_data(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);

        if ((status & ATA_STATUS_BSY) != 0) {
            continue;
        }
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0) {
            return 0;
        }
        if ((status & ATA_STATUS_DRQ) != 0) {
            return 1;
        }
    }

    return 0;
}

/* Point the bus at the master drive and the given LBA. */
static int ata_select(uint32_t lba) {
    outb(ATA_DRIVE_HEAD, (uint8_t)(ATA_DRIVE_LBA | ((lba >> 24) & 0x0F)));
    ata_delay();

    return ata_wait_idle();
}

/* Move one sector between the data port and memory, 256 words. */
static void ata_read_words(uint16_t* buffer, uint32_t words) {
    __asm__ volatile("rep insw"
                     : "+D"(buffer), "+c"(words)
                     : "d"((uint16_t)ATA_DATA)
                     : "memory");
}

static void ata_write_words(const uint16_t* buffer, uint32_t words) {
    __asm__ volatile("rep outsw"
                     : "+S"(buffer), "+c"(words)
                     : "d"((uint16_t)ATA_DATA)
                     : "memory");
}

/* Set up one command: drive, sector count, then the three LBA bytes. */
static int ata_start(uint32_t lba, uint32_t chunk, uint8_t command) {
    if (!ata_select(lba)) {
        return 0;
    }

    outb(ATA_SECTOR_COUNT, (uint8_t)(chunk == ATA_MAX_CHUNK ? 0 : chunk));
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, command);

    return 1;
}

/* Make the drive write its own cache out to the disk. */
static int ata_flush(void) {
    if (!ata_select(0)) {
        return 0;
    }

    outb(ATA_COMMAND, ATA_CMD_FLUSH);

    return ata_wait_idle();
}

/* Ask the drive who it is: model name, size, LBA support. */
static int ata_identify(void) {
    uint16_t info[256];
    uint32_t total;

    outb(ATA_CONTROL, ATA_CONTROL_NIEN);
    outb(ATA_DRIVE_HEAD, ATA_DRIVE_MASTER);
    ata_delay();

    if (inb(ATA_STATUS) == 0) {
        return 0;
    }

    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    if (!ata_wait_data()) {
        return 0;
    }

    ata_read_words(info, 256);

    if ((info[49] & 0x0200) == 0) {
        return 0;
    }

    total = (uint32_t)info[60] | ((uint32_t)info[61] << 16);
    if (total == 0) {
        return 0;
    }

    for (int i = 0; i < 40; i++) {
        uint16_t word = info[27 + i / 2];

        model[i] = (char)((i % 2 == 0) ? (word >> 8) : (word & 0xFF));
    }
    model[40] = '\0';

    for (int i = 39; i >= 0 && model[i] == ' '; i--) {
        model[i] = '\0';
    }

    sectors = total;
    return 1;
}

int ata_init(void) {
    present = 0;
    sectors = 0;
    model[0] = '\0';

    present = ata_identify();

    return present;
}

int ata_present(void) { return present; }

uint32_t ata_sector_count(void) { return sectors; }

const char* ata_model(void) { return model; }

int ata_read(uint32_t lba, void* buffer, uint32_t count) {
    uint8_t* out = (uint8_t*)buffer;

    if (!present) {
        return 0;
    }

    while (count > 0) {
        uint32_t chunk = count > ATA_MAX_CHUNK ? ATA_MAX_CHUNK : count;

        if (!ata_start(lba, chunk, ATA_CMD_READ)) {
            return 0;
        }

        for (uint32_t i = 0; i < chunk; i++) {
            if (!ata_wait_data()) {
                return 0;
            }

            ata_read_words((uint16_t*)(out + i * ATA_SECTOR_SIZE), 256);
        }

        lba += chunk;
        out += chunk * ATA_SECTOR_SIZE;
        count -= chunk;
    }

    return 1;
}

int ata_write(uint32_t lba, const void* buffer, uint32_t count) {
    const uint8_t* in = (const uint8_t*)buffer;

    if (!present) {
        return 0;
    }

    while (count > 0) {
        uint32_t chunk = count > ATA_MAX_CHUNK ? ATA_MAX_CHUNK : count;

        if (!ata_start(lba, chunk, ATA_CMD_WRITE)) {
            return 0;
        }

        for (uint32_t i = 0; i < chunk; i++) {
            if (!ata_wait_data()) {
                return 0;
            }

            ata_write_words((const uint16_t*)(in + i * ATA_SECTOR_SIZE), 256);
        }

        if (!ata_wait_idle()) {
            return 0;
        }

        lba += chunk;
        in += chunk * ATA_SECTOR_SIZE;
        count -= chunk;
    }

    return ata_flush();
}
