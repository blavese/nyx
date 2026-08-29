/* Bringing the disk up.
 *
 * Files used to be held in memory and mirrored to the disk on every change.
 * Now the VFS reads and writes FAT directly, so all that is left here is
 * deciding whether there is a usable volume, preparing one when there is
 * not, and recovering whatever an unclean shutdown stranded. */
#include "diskfs.h"
#include "fat.h"
#include "blockdev.h"
#include "printf.h"

bool diskfs_available(void) { return blk_present(); }
bool diskfs_mounted(void)   { return fat_mounted(); }
bool diskfs_flush(void)     { return blk_flush(); }

bool diskfs_format(void) {
    if (!blk_present()) return false;
    return fat_format("NYX");
}

int diskfs_mount(void) {
    if (!blk_present()) return -1;
    if (!fat_mount()) return -2;        /* not formatted, or not FAT16 */

    u32 stranded = fat_reclaim();
    if (stranded)
        kprintf("  fs      reclaimed %d cluster(s) from an unclean shutdown\n", stranded);

    return (int)fat_count("/");
}
