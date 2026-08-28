/* Persistence, backed by FAT16.
 *
 * Files are worked on in memory and written through to the disk whenever they
 * change, and read back at boot. Going through FAT rather than a private
 * layout means the image is readable by other tools, so files can move
 * between nyx and the machine hosting it. */
#include "diskfs.h"
#include "fat.h"
#include "ata.h"
#include "fs.h"
#include "heap.h"
#include "printf.h"
#include "string.h"

bool diskfs_available(void) { return ata_present(); }
bool diskfs_mounted(void)   { return fat_mounted(); }

bool diskfs_format(void) {
    if (!ata_present()) return false;
    return fat_format("NYX");
}

bool diskfs_sync(void) {
    if (!fat_mounted() && !fat_mount()) return false;

    /* Remove anything on disk that is no longer in memory, then write out
       everything that is. The filesystem is small enough that rewriting it
       wholesale is simpler than tracking which files are dirty.
       Names are collected first: deleting shifts the directory indices, so
       enumerating and deleting in the same pass would skip entries. */
    char stale[FS_MAX_FILES][FS_NAME_MAX];
    u32 stale_n = 0;

    for (u32 i = 0; stale_n < FS_MAX_FILES; i++) {
        char name[FS_NAME_MAX];
        if (fat_list(i, name, 0) != 1) break;

        bool still_here = false;
        for (u32 j = 0; j < fs_count(); j++) {
            file_t *f = fs_at(j);
            if (f && strcmp(f->name, name) == 0) { still_here = true; break; }
        }
        if (!still_here) strncpy(stale[stale_n++], name, FS_NAME_MAX - 1);
    }

    for (u32 i = 0; i < stale_n; i++) fat_delete_file(stale[i]);

    for (u32 i = 0; i < fs_count(); i++) {
        file_t *f = fs_at(i);
        if (!f) continue;
        if (!fat_write_file(f->name, f->data, f->size)) return false;
    }
    return true;
}

int diskfs_load(void) {
    if (!ata_present()) return -1;
    if (!fat_mount()) return -2;        /* not formatted, or not FAT16 */

    fs_begin_load();
    u32 loaded = 0;

    for (u32 i = 0; ; i++) {
        char name[FS_NAME_MAX];
        u32 size = 0;
        if (fat_list(i, name, &size) != 1) break;

        u8 *buf = (u8 *)kmalloc(size ? size : 1);
        if (!buf) break;
        int got = fat_read_file(name, buf, size);
        if (got >= 0) { fs_write(name, buf, (u32)got); loaded++; }
        kfree(buf);
    }

    fs_end_load();
    return (int)loaded;
}
