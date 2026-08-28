#pragma once
#include "types.h"
#include "wm.h"

/* The window server: the half of the window manager that ring 3 can reach.
   It hands out window handles rather than pointers, and maps a window's
   pixels into the calling program's address space. */

#define WINSRV_MAX 8

/* Where a program's first surface appears in its own address space. Each
   further window starts a megabyte higher. */
#define WINSRV_SURFACE_BASE 0x60000000u
#define WINSRV_SURFACE_STEP 0x00100000u

void winsrv_init(void);

int  winsrv_create(u32 pid, const char *title, int cw, int ch);
u32  winsrv_surface(u32 pid, int handle, u32 dir);   /* user address, or 0 */
int  winsrv_size(u32 pid, int handle);               /* cw << 16 | ch, or -1 */
bool winsrv_poll(u32 pid, int handle, wm_event_t *out);
bool winsrv_commit(u32 pid, int handle);
bool winsrv_close(u32 pid, int handle);

/* The window behind a handle, for kernel-side callers. Null if the handle is
   not this program's, or the window has already gone. */
window_t *winsrv_window(u32 pid, int handle);

/* Drops every window a task owned. Called when the task goes away, so a
   program that crashes does not leave a dead window on the desktop. */
void winsrv_release(u32 pid);
