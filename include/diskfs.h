#pragma once
#include "types.h"

bool diskfs_available(void);
bool diskfs_mounted(void);
bool diskfs_format(void);
bool diskfs_sync(void);
int  diskfs_load(void);      /* files loaded, or negative on error */
