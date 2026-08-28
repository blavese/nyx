/* Installing the built-in programs.
 *
 * builtin.S pastes the ELF files into the kernel image; this puts them in the
 * filesystem under their normal names.
 *
 * The kernel's copy always wins, and is never written to the disk. Otherwise
 * the first boot would save these programs and every later boot would run the
 * saved ones, so rebuilding the kernel would appear to change nothing. Any
 * copy an older build left behind is deleted at the next sync. */
#include "builtin.h"
#include "fs.h"
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
    { "hello.elf", builtin_hello_start, builtin_hello_end },
    { "count.elf", builtin_count_start, builtin_count_end },
    { "wintest.elf", builtin_wintest_start, builtin_wintest_end },
    { "paint.elf", builtin_paint_start, builtin_paint_end },
};

#define N_PROGRAMS (sizeof(PROGRAMS) / sizeof(PROGRAMS[0]))

u32 builtin_count_programs(void) { return N_PROGRAMS; }

void builtin_install(void) {
    /* Installing is not a change worth writing back, and the write-back is
       what this is avoiding in the first place. */
    fs_begin_load();
    for (u32 i = 0; i < N_PROGRAMS; i++) {
        const program_t *p = &PROGRAMS[i];
        if (!fs_write(p->name, p->start, (u32)(p->end - p->start))) continue;
        file_t *f = fs_find(p->name);
        if (f) f->builtin = true;
    }
    fs_end_load();
}
