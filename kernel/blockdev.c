/* Picks a disk controller and hides which one it was from the filesystem.
 *
 * AHCI is tried first because that is what modern machines and modern virtual
 * machines present; the ATA PIO driver stays as the fallback for QEMU's
 * legacy default and for genuinely old hardware. */
#include "blockdev.h"
#include "ahci.h"
#include "ata.h"

typedef enum { DISK_NONE, DISK_AHCI, DISK_ATA } disk_t;
static disk_t disk = DISK_NONE;

bool blk_init(void) {
    if (ahci_init()) { disk = DISK_AHCI; return true; }
    if (ata_init())  { disk = DISK_ATA;  return true; }
    disk = DISK_NONE;
    return false;
}

bool blk_present(void) {
    switch (disk) {
        case DISK_AHCI: return ahci_present();
        case DISK_ATA:  return ata_present();
        default:        return false;
    }
}

u32 blk_sectors(void) {
    switch (disk) {
        case DISK_AHCI: return ahci_sectors();
        case DISK_ATA:  return ata_sectors();
        default:        return 0;
    }
}

const char *blk_model(void) {
    switch (disk) {
        case DISK_AHCI: return ahci_model();
        case DISK_ATA:  return ata_model();
        default:        return "";
    }
}

const char *blk_driver(void) {
    switch (disk) {
        case DISK_AHCI: return "ahci";
        case DISK_ATA:  return "ata";
        default:        return "none";
    }
}

bool blk_read(u32 lba, u32 count, void *buf) {
    switch (disk) {
        case DISK_AHCI: return ahci_read(lba, count, buf);
        case DISK_ATA:  return ata_read(lba, count, buf);
        default:        return false;
    }
}

bool blk_write(u32 lba, u32 count, const void *buf) {
    switch (disk) {
        case DISK_AHCI: return ahci_write(lba, count, buf);
        case DISK_ATA:  return ata_write(lba, count, buf);
        default:        return false;
    }
}

bool blk_flush(void) {
    switch (disk) {
        case DISK_AHCI: return ahci_flush();
        case DISK_ATA:  return ata_flush();
        default:        return false;
    }
}
