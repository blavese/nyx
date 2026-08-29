#pragma once
#include "types.h"

/* The in-memory filesystem.
 *
 * This is the backend the VFS falls back to when there is no disk, so a
 * machine without one still works rather than half working. Names here are
 * whole absolute paths, and a directory is an entry with no contents, which
 * is enough to list and navigate. Nothing here survives a reboot. */

#define FS_MAX_FILES 64
#define FS_NAME_MAX  128

typedef struct {
    char name[FS_NAME_MAX];      /* an absolute path, not a bare name */
    u8  *data;
    u32  size;
    u32  cap;
    bool used;
    bool is_dir;
} file_t;

void    fs_init(void);
file_t *fs_find(const char *path);
file_t *fs_create(const char *path);
bool    fs_write(const char *path, const void *buf, u32 len);
bool    fs_append(const char *path, const void *buf, u32 len);
bool    fs_delete(const char *path);
bool    fs_mkdir(const char *path);
u32     fs_count(void);
file_t *fs_at(u32 i);
u32     fs_bytes_used(void);
