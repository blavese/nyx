#pragma once
#include "types.h"

#define FS_MAX_FILES 64
#define FS_NAME_MAX  32

typedef struct {
    char name[FS_NAME_MAX];
    u8  *data;
    u32  size;
    u32  cap;
    bool used;
    bool builtin;      /* came from the kernel image, so it is never saved */
} file_t;

void    fs_init(void);
file_t *fs_find(const char *name);
file_t *fs_create(const char *name);
bool    fs_write(const char *name, const void *buf, u32 len);
bool    fs_append(const char *name, const void *buf, u32 len);
bool    fs_delete(const char *name);
u32     fs_count(void);
file_t *fs_at(u32 i);
u32     fs_bytes_used(void);
void    fs_begin_load(void);
void    fs_end_load(void);
