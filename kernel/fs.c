/* The in-memory filesystem.
 *
 * Files live on the kernel heap and do not survive a reboot. This used to be
 * the whole filesystem, mirrored to the disk on every change; now the VFS
 * writes to FAT directly and this is only what answers when no disk exists.
 * That is a better division: one storage layer per kind of storage, rather
 * than one pretending to be the other. */
#include "fs.h"
#include "heap.h"
#include "string.h"

static file_t files[FS_MAX_FILES];

void fs_init(void) { memset(files, 0, sizeof(files)); }

file_t *fs_find(const char *path) {
    for (u32 i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && strcmp(files[i].name, path) == 0) return &files[i];
    return 0;
}

file_t *fs_create(const char *path) {
    file_t *f = fs_find(path);
    if (f) return f;
    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(file_t));
            strncpy(files[i].name, path, FS_NAME_MAX - 1);
            files[i].used = true;
            return &files[i];
        }
    }
    return 0;
}

static bool ensure(file_t *f, u32 need) {
    if (f->cap >= need) return true;
    u32 cap = f->cap ? f->cap : 64;
    while (cap < need) cap *= 2;
    u8 *nd = (u8 *)kmalloc(cap);
    if (!nd) return false;
    if (f->data) { memcpy(nd, f->data, f->size); kfree(f->data); }
    f->data = nd;
    f->cap = cap;
    return true;
}

bool fs_write(const char *path, const void *buf, u32 len) {
    file_t *f = fs_create(path);
    if (!f || f->is_dir || !ensure(f, len ? len : 1)) return false;
    memcpy(f->data, buf, len);
    f->size = len;
    return true;
}

bool fs_append(const char *path, const void *buf, u32 len) {
    file_t *f = fs_create(path);
    if (!f || f->is_dir || !ensure(f, f->size + len)) return false;
    memcpy(f->data + f->size, buf, len);
    f->size += len;
    return true;
}

bool fs_mkdir(const char *path) {
    if (fs_find(path)) return false;
    file_t *f = fs_create(path);
    if (!f) return false;
    f->is_dir = true;
    return true;
}

bool fs_delete(const char *path) {
    file_t *f = fs_find(path);
    if (!f) return false;
    if (f->data) kfree(f->data);
    memset(f, 0, sizeof(*f));
    return true;
}

u32 fs_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < FS_MAX_FILES; i++) if (files[i].used) n++;
    return n;
}

file_t *fs_at(u32 i) {
    u32 n = 0;
    for (u32 k = 0; k < FS_MAX_FILES; k++)
        if (files[k].used && n++ == i) return &files[k];
    return 0;
}

u32 fs_bytes_used(void) {
    u32 n = 0;
    for (u32 i = 0; i < FS_MAX_FILES; i++) if (files[i].used) n += files[i].size;
    return n;
}
