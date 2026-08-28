/* FAT16.
 *
 * The point of this over the previous custom format is interoperability: a
 * FAT image can be opened by other tools, so files can move between nyx and
 * the machine hosting it. The format is old and fiddly but exhaustively
 * documented, and every field below is laid out where the specification says
 * it goes, because anything else produces an image that other readers call
 * corrupt.
 *
 * Only the root directory is supported. There are no subdirectories. */
#include "fat.h"
#include "ata.h"
#include "blockdev.h"
#include "heap.h"
#include "printf.h"
#include "string.h"

#define ATTR_READONLY  0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LFN       0x0F

#define ENT_FREE      0x00
#define ENT_DELETED   0xE5

#define EOC_MIN 0xFFF8u
#define EOC     0xFFFFu

typedef struct {
    u8  name[11];
    u8  attr;
    u8  nt_reserved;
    u8  create_tenth;
    u16 create_time, create_date;
    u16 access_date;
    u16 cluster_hi;          /* always zero on FAT16 */
    u16 write_time, write_date;
    u16 cluster_lo;
    u32 size;
} __attribute__((packed)) dirent_t;

static bool  mounted;
static u16   bytes_per_sector;
static u8    sectors_per_cluster;
static u16   reserved_sectors;
static u8    num_fats;
static u16   root_entries;
static u32   total_sectors;
static u32   fat_sectors;
static u32   fat_start;
static u32   root_start;
static u32   root_sectors;
static u32   data_start;
static u32   cluster_count;

static u8 sec[SECTOR_SIZE];

bool fat_mounted(void) { return mounted; }
u32  fat_total_clusters(void) { return cluster_count; }
u32  fat_cluster_bytes(void) { return (u32)sectors_per_cluster * SECTOR_SIZE; }

/* --- 8.3 names ---------------------------------------------------------- */

static char upcase(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* "readme.txt" becomes "README  TXT". Anything that will not fit is
   truncated rather than rejected, which matches what DOS did. */
static void to_83(const char *name, u8 out[11]) {
    memset(out, ' ', 11);
    u32 i = 0;
    while (name[i] && name[i] != '.' && i < 8) { out[i] = (u8)upcase(name[i]); i++; }
    const char *dot = name;
    while (*dot && *dot != '.') dot++;
    if (*dot == '.') {
        dot++;
        for (u32 j = 0; j < 3 && dot[j]; j++) out[8 + j] = (u8)upcase(dot[j]);
    }
}

static char downcase(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* FAT stores names folded to upper case. Handing "README.TXT" back to a
   case sensitive filesystem would make it a different file from the
   "readme.txt" that created it, so fold the other way on the way out. */
static void from_83(const u8 in[11], char *out) {
    u32 n = 0;
    for (u32 i = 0; i < 8 && in[i] != ' '; i++) out[n++] = downcase((char)in[i]);
    if (in[8] != ' ') {
        out[n++] = '.';
        for (u32 i = 8; i < 11 && in[i] != ' '; i++) out[n++] = downcase((char)in[i]);
    }
    out[n] = 0;
}

/* --- the file allocation table ------------------------------------------ */

static u16 fat_get(u32 cluster) {
    u32 off = cluster * 2;
    u32 lba = fat_start + off / SECTOR_SIZE;
    if (!blk_read(lba, 1, sec)) return EOC;
    return *(u16 *)(sec + (off % SECTOR_SIZE));
}

static bool fat_set(u32 cluster, u16 value) {
    u32 off = cluster * 2;
    /* Both copies of the table have to agree or other readers will object. */
    for (u32 copy = 0; copy < num_fats; copy++) {
        u32 lba = fat_start + copy * fat_sectors + off / SECTOR_SIZE;
        if (!blk_read(lba, 1, sec)) return false;
        *(u16 *)(sec + (off % SECTOR_SIZE)) = value;
        if (!blk_write(lba, 1, sec)) return false;
    }
    return true;
}

static u32 alloc_cluster(void) {
    for (u32 c = 2; c < cluster_count + 2; c++) {
        if (fat_get(c) == 0) {
            if (!fat_set(c, EOC)) return 0;
            return c;
        }
    }
    return 0;
}

static void free_chain(u32 cluster) {
    while (cluster >= 2 && cluster < EOC_MIN) {
        u16 next = fat_get(cluster);
        fat_set(cluster, 0);
        cluster = next;
    }
}

static u32 cluster_lba(u32 cluster) {
    return data_start + (cluster - 2) * sectors_per_cluster;
}

/* --- mounting ----------------------------------------------------------- */

bool fat_mount(void) {
    mounted = false;
    if (!blk_present()) return false;
    if (!blk_read(0, 1, sec)) return false;

    if (sec[510] != 0x55 || sec[511] != 0xAA) return false;

    bytes_per_sector    = *(u16 *)(sec + 11);
    sectors_per_cluster = sec[13];
    reserved_sectors    = *(u16 *)(sec + 14);
    num_fats            = sec[16];
    root_entries        = *(u16 *)(sec + 17);
    u16 total16         = *(u16 *)(sec + 19);
    fat_sectors         = *(u16 *)(sec + 22);
    u32 total32         = *(u32 *)(sec + 32);

    total_sectors = total16 ? total16 : total32;

    if (bytes_per_sector != SECTOR_SIZE) return false;
    if (sectors_per_cluster == 0 || num_fats == 0 || fat_sectors == 0) return false;

    fat_start    = reserved_sectors;
    root_sectors = ((u32)root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    root_start   = fat_start + (u32)num_fats * fat_sectors;
    data_start   = root_start + root_sectors;

    if (data_start >= total_sectors) return false;
    cluster_count = (total_sectors - data_start) / sectors_per_cluster;

    /* FAT16 is defined by its cluster count, not by what the label claims. */
    if (cluster_count < 4085 || cluster_count > 65524) return false;

    mounted = true;
    return true;
}

bool fat_format(const char *label) {
    if (!blk_present()) return false;

    u32 total = blk_sectors();
    if (total < 8192) return false;

    u8 spc = 4;                       /* 2 KiB clusters */
    u16 reserved = 1;
    u8 fats = 2;
    u16 roots = 512;
    u32 root_secs = ((u32)roots * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;

    /* The table has to be big enough to describe the clusters that are left
       after the table itself is subtracted, so solve for it. */
    u32 fsize = 1;
    for (int i = 0; i < 16; i++) {
        u32 data = total - reserved - (u32)fats * fsize - root_secs;
        u32 clusters = data / spc;
        u32 need = ((clusters + 2) * 2 + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (need == fsize) break;
        fsize = need;
    }

    u32 data = total - reserved - (u32)fats * fsize - root_secs;
    u32 clusters = data / spc;
    if (clusters < 4085 || clusters > 65524) return false;

    /* boot sector */
    memset(sec, 0, SECTOR_SIZE);
    sec[0] = 0xEB; sec[1] = 0x3C; sec[2] = 0x90;
    memcpy(sec + 3, "NYX     ", 8);
    *(u16 *)(sec + 11) = SECTOR_SIZE;
    sec[13] = spc;
    *(u16 *)(sec + 14) = reserved;
    sec[16] = fats;
    *(u16 *)(sec + 17) = roots;
    *(u16 *)(sec + 19) = 0;
    sec[21] = 0xF8;                     /* fixed disk */
    *(u16 *)(sec + 22) = (u16)fsize;
    *(u16 *)(sec + 24) = 32;
    *(u16 *)(sec + 26) = 8;
    *(u32 *)(sec + 28) = 0;
    *(u32 *)(sec + 32) = total;
    sec[36] = 0x80;
    sec[38] = 0x29;                     /* extended boot signature */
    *(u32 *)(sec + 39) = 0x4E595800u;
    memset(sec + 43, ' ', 11);
    for (u32 i = 0; i < 11 && label && label[i]; i++) sec[43 + i] = (u8)upcase(label[i]);
    memcpy(sec + 54, "FAT16   ", 8);
    sec[510] = 0x55; sec[511] = 0xAA;
    if (!blk_write(0, 1, sec)) return false;

    /* both tables, cleared, with the two reserved entries at the front */
    memset(sec, 0, SECTOR_SIZE);
    for (u32 copy = 0; copy < fats; copy++)
        for (u32 s = 0; s < fsize; s++)
            if (!blk_write(reserved + copy * fsize + s, 1, sec)) return false;

    memset(sec, 0, SECTOR_SIZE);
    *(u16 *)(sec + 0) = 0xFFF8;         /* media descriptor copy */
    *(u16 *)(sec + 2) = 0xFFFF;         /* end of chain marker */
    for (u32 copy = 0; copy < fats; copy++)
        if (!blk_write(reserved + copy * fsize, 1, sec)) return false;

    /* empty root directory */
    memset(sec, 0, SECTOR_SIZE);
    for (u32 s = 0; s < root_secs; s++)
        if (!blk_write(reserved + (u32)fats * fsize + s, 1, sec)) return false;

    blk_flush();
    return fat_mount();
}

/* --- directory ---------------------------------------------------------- */

static bool root_read(u32 index, dirent_t *out) {
    if (index >= root_entries) return false;
    u32 per = SECTOR_SIZE / 32;
    if (!blk_read(root_start + index / per, 1, sec)) return false;
    memcpy(out, sec + (index % per) * 32, 32);
    return true;
}

static bool root_write(u32 index, const dirent_t *in) {
    if (index >= root_entries) return false;
    u32 per = SECTOR_SIZE / 32;
    u32 lba = root_start + index / per;
    if (!blk_read(lba, 1, sec)) return false;
    memcpy(sec + (index % per) * 32, in, 32);
    return blk_write(lba, 1, sec);
}

static int find_entry(const char *name, dirent_t *out) {
    u8 want[11];
    to_83(name, want);
    dirent_t e;
    for (u32 i = 0; i < root_entries; i++) {
        if (!root_read(i, &e)) break;
        if (e.name[0] == ENT_FREE) break;
        if (e.name[0] == ENT_DELETED) continue;
        if ((e.attr & ATTR_LFN) == ATTR_LFN) continue;
        if (e.attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY)) continue;
        if (memcmp(e.name, want, 11) == 0) { if (out) *out = e; return (int)i; }
    }
    return -1;
}

int fat_list(u32 index, char *name_out, u32 *size_out) {
    if (!mounted) return -1;
    dirent_t e;
    u32 seen = 0;
    for (u32 i = 0; i < root_entries; i++) {
        if (!root_read(i, &e)) break;
        if (e.name[0] == ENT_FREE) break;
        if (e.name[0] == ENT_DELETED) continue;
        if ((e.attr & ATTR_LFN) == ATTR_LFN) continue;
        if (e.attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY)) continue;
        if (seen == index) {
            if (name_out) from_83(e.name, name_out);
            if (size_out) *size_out = e.size;
            return 1;
        }
        seen++;
    }
    return 0;
}

u32 fat_count(void) {
    u32 n = 0;
    while (fat_list(n, 0, 0) == 1) n++;
    return n;
}

/* --- files -------------------------------------------------------------- */

int fat_read_file(const char *name, u8 *buf, u32 cap) {
    if (!mounted) return -1;
    dirent_t e;
    if (find_entry(name, &e) < 0) return -1;

    u32 want = e.size < cap ? e.size : cap;
    u32 done = 0;
    u32 cluster = e.cluster_lo;

    while (done < want && cluster >= 2 && cluster < EOC_MIN) {
        u32 lba = cluster_lba(cluster);
        for (u32 s = 0; s < sectors_per_cluster && done < want; s++) {
            if (!blk_read(lba + s, 1, sec)) return -1;
            u32 n = want - done;
            if (n > SECTOR_SIZE) n = SECTOR_SIZE;
            memcpy(buf + done, sec, n);
            done += n;
        }
        cluster = fat_get(cluster);
    }
    return (int)done;
}

bool fat_write_file(const char *name, const u8 *buf, u32 size) {
    if (!mounted) return false;

    dirent_t e;
    int slot = find_entry(name, &e);
    u32 old_chain = 0;

    if (slot >= 0) {
        /* Remember the old chain but do not touch it yet. */
        old_chain = e.cluster_lo;
    } else {
        memset(&e, 0, sizeof(e));
        for (u32 i = 0; i < root_entries; i++) {
            dirent_t probe;
            if (!root_read(i, &probe)) break;
            if (probe.name[0] == ENT_FREE || probe.name[0] == ENT_DELETED) { slot = (int)i; break; }
        }
        if (slot < 0) return false;
        to_83(name, e.name);
        e.attr = ATTR_ARCHIVE;
    }

    /* Write the new copy first, into clusters nothing points at yet. Losing
       power during this stage leaves the directory still describing the old
       file, so the old contents survive intact. */
    u32 first = 0, prev = 0, written = 0;
    u32 per_cluster = fat_cluster_bytes();

    while (written < size) {
        u32 c = alloc_cluster();
        if (!c) { if (first) free_chain(first); return false; }
        if (prev) fat_set(prev, (u16)c);
        else first = c;
        prev = c;

        u32 lba = cluster_lba(c);
        for (u32 s = 0; s < sectors_per_cluster; s++) {
            memset(sec, 0, SECTOR_SIZE);
            u32 off = written + s * SECTOR_SIZE;
            if (off < size) {
                u32 n = size - off;
                if (n > SECTOR_SIZE) n = SECTOR_SIZE;
                memcpy(sec, buf + off, n);
            }
            if (!blk_write(lba + s, 1, sec)) { if (first) free_chain(first); return false; }
        }
        written += per_cluster;
    }

    /* Make sure the data is on the platter before anything points at it. */
    blk_flush();

    e.cluster_lo = (u16)first;
    e.cluster_hi = 0;
    e.size = size;
    e.write_date = 0x5A21;
    e.write_time = 0;

    /* One sector write swings the file from the old chain to the new one.
       This is the commit: before it the old file is live, after it the new
       one is, and there is no moment where neither is. */
    if (!root_write((u32)slot, &e)) { if (first) free_chain(first); return false; }
    if (!blk_flush()) return false;

    /* Only now is the old chain unreachable and safe to release. A crash
       before this point leaks clusters, which fat_reclaim recovers; it never
       loses the file. */
    if (old_chain >= 2) { free_chain(old_chain); blk_flush(); }
    return true;
}

/* Frees clusters that no directory entry refers to.
 *
 * A crash between writing a new copy and committing it leaves its clusters
 * allocated but unreachable. Nothing is corrupt, but the space is gone until
 * somebody notices, so this runs at mount. */
u32 fat_reclaim(void) {
    if (!mounted) return 0;

    u32 total = cluster_count + 2;
    u8 *reachable = (u8 *)kmalloc(total);
    if (!reachable) return 0;
    memset(reachable, 0, total);

    reachable[0] = reachable[1] = 1;          /* the two reserved entries */

    dirent_t e;
    for (u32 i = 0; i < root_entries; i++) {
        if (!root_read(i, &e)) break;
        if (e.name[0] == ENT_FREE) break;
        if (e.name[0] == ENT_DELETED) continue;
        if ((e.attr & ATTR_LFN) == ATTR_LFN) continue;
        if (e.attr & ATTR_VOLUME_ID) continue;

        u32 c = e.cluster_lo;
        u32 guard = 0;
        while (c >= 2 && c < EOC_MIN && c < total && guard++ < total) {
            if (reachable[c]) break;          /* a loop; stop rather than spin */
            reachable[c] = 1;
            c = fat_get(c);
        }
    }

    u32 freed = 0;
    for (u32 c = 2; c < total; c++) {
        if (reachable[c]) continue;
        if (fat_get(c) == 0) continue;        /* already free */
        fat_set(c, 0);
        freed++;
    }
    if (freed) blk_flush();

    kfree(reachable);
    return freed;
}

bool fat_delete_file(const char *name) {
    if (!mounted) return false;
    dirent_t e;
    int slot = find_entry(name, &e);
    if (slot < 0) return false;
    if (e.cluster_lo >= 2) free_chain(e.cluster_lo);
    e.name[0] = ENT_DELETED;
    if (!root_write((u32)slot, &e)) return false;
    return blk_flush();
}

u32 fat_free_bytes(void) {
    if (!mounted) return 0;
    u32 free_clusters = 0;
    for (u32 c = 2; c < cluster_count + 2; c++)
        if (fat_get(c) == 0) free_clusters++;
    return free_clusters * fat_cluster_bytes();
}
