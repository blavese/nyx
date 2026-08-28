/* Persistence for the filesystem.
 *
 * The whole filesystem is small enough to hold in memory, so rather than
 * maintaining on-disk structures live, the kernel keeps working in RAM and
 * writes the entire thing out as one image whenever it changes. That trades
 * write throughput, which nothing here needs, for a format simple enough to
 * be obviously correct.
 *
 *   sector 0        header
 *   sectors 1..8    64 file records, 64 bytes each
 *   sector 9 on     file contents, each starting on a sector boundary
 */
#include "diskfs.h"
#include "ata.h"
#include "fs.h"
#include "heap.h"
#include "printf.h"
#include "string.h"

#define NYXFS_MAGIC   0x5346584Eu      /* "NXFS" */
#define NYXFS_VERSION 1
#define REC_SECTORS   8
#define DATA_START    9
#define REC_SIZE      64

typedef struct {
    u32 magic;
    u32 version;
    u32 file_count;
    u32 total_bytes;
    u32 reserved[124];
} __attribute__((packed)) header_t;

typedef struct {
    char name[FS_NAME_MAX];
    u32  size;
    u32  start_sector;
    u32  reserved[6];
} __attribute__((packed)) record_t;

static bool mounted = false;

bool diskfs_available(void) { return ata_present(); }
bool diskfs_mounted(void)   { return mounted; }

static u32 sectors_for(u32 bytes) { return (bytes + SECTOR_SIZE - 1) / SECTOR_SIZE; }

bool diskfs_format(void) {
    if (!ata_present()) return false;

    header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = NYXFS_MAGIC;
    h.version = NYXFS_VERSION;
    h.file_count = 0;
    h.total_bytes = 0;

    u8 sector[SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    memcpy(sector, &h, sizeof(h) > SECTOR_SIZE ? SECTOR_SIZE : sizeof(h));
    if (!ata_write(0, 1, sector)) return false;

    memset(sector, 0, sizeof(sector));
    for (u32 i = 0; i < REC_SECTORS; i++)
        if (!ata_write(1 + i, 1, sector)) return false;

    mounted = true;
    return true;
}

bool diskfs_sync(void) {
    if (!ata_present()) return false;

    u32 count = fs_count();
    if (count > 64) count = 64;

    /* Lay the files out one after another, sector aligned. */
    u8 recbuf[REC_SECTORS * SECTOR_SIZE];
    memset(recbuf, 0, sizeof(recbuf));

    u32 cursor = DATA_START;
    u32 total = 0;

    for (u32 i = 0; i < count; i++) {
        file_t *f = fs_at(i);
        if (!f) continue;
        record_t *r = (record_t *)(recbuf + i * REC_SIZE);
        strncpy(r->name, f->name, FS_NAME_MAX - 1);
        r->size = f->size;
        r->start_sector = cursor;

        u32 need = sectors_for(f->size);
        if (need == 0) need = 1;
        if (cursor + need > ata_sectors()) return false;

        /* Write the payload a sector at a time, padding the last one. */
        u8 tmp[SECTOR_SIZE];
        for (u32 s = 0; s < need; s++) {
            memset(tmp, 0, SECTOR_SIZE);
            u32 off = s * SECTOR_SIZE;
            u32 n = f->size > off ? f->size - off : 0;
            if (n > SECTOR_SIZE) n = SECTOR_SIZE;
            if (n) memcpy(tmp, f->data + off, n);
            if (!ata_write(cursor + s, 1, tmp)) return false;
        }
        cursor += need;
        total += f->size;
    }

    for (u32 i = 0; i < REC_SECTORS; i++)
        if (!ata_write(1 + i, 1, recbuf + i * SECTOR_SIZE)) return false;

    header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = NYXFS_MAGIC;
    h.version = NYXFS_VERSION;
    h.file_count = count;
    h.total_bytes = total;

    u8 sector[SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    memcpy(sector, &h, sizeof(header_t) > SECTOR_SIZE ? SECTOR_SIZE : sizeof(header_t));
    if (!ata_write(0, 1, sector)) return false;

    mounted = true;
    return ata_flush();
}

int diskfs_load(void) {
    if (!ata_present()) return -1;

    u8 sector[SECTOR_SIZE];
    if (!ata_read(0, 1, sector)) return -1;

    header_t *h = (header_t *)sector;
    if (h->magic != NYXFS_MAGIC) return -2;        /* not formatted */
    if (h->version != NYXFS_VERSION) return -3;

    u32 count = h->file_count;
    if (count > 64) count = 64;

    u8 recbuf[REC_SECTORS * SECTOR_SIZE];
    for (u32 i = 0; i < REC_SECTORS; i++)
        if (!ata_read(1 + i, 1, recbuf + i * SECTOR_SIZE)) return -1;

    fs_begin_load();
    u32 loaded = 0;

    for (u32 i = 0; i < count; i++) {
        record_t *r = (record_t *)(recbuf + i * REC_SIZE);
        if (r->name[0] == 0) continue;
        r->name[FS_NAME_MAX - 1] = 0;

        u32 need = sectors_for(r->size);
        if (need == 0) need = 1;

        u8 *buf = (u8 *)kmalloc(need * SECTOR_SIZE);
        if (!buf) break;

        bool ok = true;
        for (u32 s = 0; s < need && ok; s++)
            ok = ata_read(r->start_sector + s, 1, buf + s * SECTOR_SIZE);

        if (ok) { fs_write(r->name, buf, r->size); loaded++; }
        kfree(buf);
    }

    fs_end_load();
    mounted = true;
    return (int)loaded;
}
