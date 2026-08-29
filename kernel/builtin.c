/* Registering the built-in programs.
 *
 * builtin.S pastes the ELF files into the kernel image; this hands them to
 * the VFS, which lists them in the root and serves reads straight out of the
 * kernel image.
 *
 * They are deliberately not copied onto the disk. If they were, the first
 * boot would write them out and every later boot would run the written
 * copies, so rebuilding the kernel would appear to change nothing. */
#include "builtin.h"
#include "vfs.h"
#include "printf.h"

extern const u8 builtin_hello_start[], builtin_hello_end[];
extern const u8 builtin_count_start[], builtin_count_end[];
extern const u8 builtin_wintest_start[], builtin_wintest_end[];
extern const u8 builtin_paint_start[], builtin_paint_end[];

typedef struct {
    const char *name;
    const u8   *start, *end;
} program_t;

static const program_t PROGRAMS[] = {
    { "hello.elf",   builtin_hello_start,   builtin_hello_end },
    { "count.elf",   builtin_count_start,   builtin_count_end },
    { "wintest.elf", builtin_wintest_start, builtin_wintest_end },
    { "paint.elf",   builtin_paint_start,   builtin_paint_end },
};

#define N_PROGRAMS (sizeof(PROGRAMS) / sizeof(PROGRAMS[0]))

u32 builtin_count_programs(void) { return N_PROGRAMS; }

void builtin_install(void) {
    for (u32 i = 0; i < N_PROGRAMS; i++) {
        const program_t *p = &PROGRAMS[i];
        vfs_add_builtin(p->name, p->start, (u32)(p->end - p->start));
    }
}
