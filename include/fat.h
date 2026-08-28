#pragma once
#include "types.h"

bool fat_mount(void);
bool fat_mounted(void);
bool fat_format(const char *label);

/* Returns 1 when an entry was produced, 0 at the end of the directory. */
int  fat_list(u32 index, char *name_out, u32 *size_out);
u32  fat_count(void);

int  fat_read_file(const char *name, u8 *buf, u32 cap);   /* bytes, or -1 */
bool fat_write_file(const char *name, const u8 *buf, u32 size);
bool fat_delete_file(const char *name);

u32  fat_total_clusters(void);
u32  fat_cluster_bytes(void);
u32  fat_free_bytes(void);
