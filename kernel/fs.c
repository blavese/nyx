/* A flat in-memory filesystem. Files live on the kernel heap and do not
   survive a reboot; there is no block device driver yet. */
#include "fs.h"
#include "heap.h"
#include "string.h"

static file_t files[FS_MAX_FILES];

void fs_init(void) { memset(files, 0, sizeof(files)); }

file_t *fs_find(const char *name) {
    for (u32 i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && strcmp(files[i].name, name) == 0) return &files[i];
    return 0;
}

file_t *fs_create(const char *name) {
    file_t *f = fs_find(name);
    if (f) return f;
    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(file_t));
            strncpy(files[i].name, name, FS_NAME_MAX - 1);
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

bool fs_write(const char *name, const void *buf, u32 len) {
    file_t *f = fs_create(name);
    if (!f || !ensure(f, len)) return false;
    memcpy(f->data, buf, len);
    f->size = len;
    return true;
}

bool fs_append(const char *name, const void *buf, u32 len) {
    file_t *f = fs_create(name);
    if (!f || !ensure(f, f->size + len)) return false;
    memcpy(f->data + f->size, buf, len);
    f->size += len;
    return true;
}

bool fs_delete(const char *name) {
    file_t *f = fs_find(name);
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
