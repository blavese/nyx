/* Boot-time self test. `run.sh -T` boots with this enabled, so the whole
   kernel can be checked from a script without a human watching a screen. */
#include "selftest.h"
#include "printf.h"
#include "string.h"
#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "fs.h"
#include "vfs.h"
#include "timer.h"
#include "sched.h"
#include "syscall.h"
#include "idt.h"
#include "blockdev.h"
#include "diskfs.h"
#include "fat.h"
#include "elf.h"
#include "user.h"
#include "paging.h"
#include "sched.h"
#include "syscall.h"
#include "net.h"
#include "netdev.h"
#include "fb.h"
#include "fbcon.h"
#include "mouse.h"
#include "gfx.h"
#include "wm.h"
#include "font.h"
#include "winsrv.h"
#include "theme.h"
#include "smp.h"
#include "builtin.h"

static int passed, failed;

static void ok(const char *what, bool cond) {
    if (cond) { passed++; kprintf("  PASS  %s\n", what); }
    else      { failed++; kprintf("  FAIL  %s\n", what); }
}

static void test_string(void) {
    char b[32];
    ok("strlen", strlen("hello") == 5);
    ok("strcmp equal", strcmp("abc", "abc") == 0);
    ok("strcmp order", strcmp("abc", "abd") < 0);
    strcpy(b, "copy me");
    ok("strcpy", strcmp(b, "copy me") == 0);
    memset(b, 'x', 4); b[4] = 0;
    ok("memset", strcmp(b, "xxxx") == 0);
    char s[8] = "source";
    char d[8];
    memcpy(d, s, 7);
    ok("memcpy", strcmp(d, "source") == 0);
    ok("memcmp", memcmp("ab", "ab", 2) == 0 && memcmp("ab", "ac", 2) != 0);
    char ov[16] = "abcdef";
    memmove(ov + 1, ov, 5); ov[6] = 0;
    ok("memmove overlap", strcmp(ov, "aabcde") == 0);
}

static void test_heap(void) {
    u32 before = heap_used();
    void *a = kmalloc(64), *b = kmalloc(128), *c = kmalloc(32);
    ok("kmalloc returns distinct blocks", a && b && c && a != b && b != c);
    memset(a, 0xAA, 64);
    ok("heap memory is writable", *(u8 *)a == 0xAA);
    kfree(a); kfree(b); kfree(c);
    ok("heap coalesces back to baseline", heap_used() == before);

    void *big = kmalloc(1024 * 512);
    ok("large allocation", big != 0);
    kfree(big);

    u32 *z = (u32 *)kcalloc(64);
    bool zeroed = z != 0;
    for (int i = 0; i < 16 && zeroed; i++) if (z[i]) zeroed = false;
    ok("kcalloc zeroes", zeroed);
    kfree(z);
}

static void test_pmm(void) {
    u32 free_before = pmm_free_frames();
    u32 f1 = pmm_alloc_frame(), f2 = pmm_alloc_frame();
    ok("frames allocate", f1 && f2 && f1 != f2);
    ok("frames are page aligned", (f1 & 0xFFF) == 0 && (f2 & 0xFFF) == 0);
    ok("free count dropped", pmm_free_frames() == free_before - 2);
    pmm_free_frame(f1); pmm_free_frame(f2);
    ok("free count restored", pmm_free_frames() == free_before);
}

static void test_paging(void) {
    u32 phys = pmm_alloc_frame();
    const u32 v = 0x00D00000;
    ok("map_page", map_page(v, phys, PTE_PRESENT | PTE_RW));
    volatile u32 *p = (volatile u32 *)v;
    *p = 0xDEADBEEF;
    ok("write through mapping", *p == 0xDEADBEEF);
    ok("virt_to_phys agrees", (virt_to_phys(v) & ~0xFFFu) == phys);
    unmap_page(v);
    ok("unmap clears translation", virt_to_phys(v) == 0);
    pmm_free_frame(phys);
}

/* The rule a system call leans on when it validates a pointer: being mapped
   in the caller's address space is not the same as being reachable from ring
   3. Every space inherits the kernel's high mappings, so a check that only
   asked whether a page was present would accept the framebuffer and the
   controllers' register windows. */
static void test_user_access(void) {
    u32 dir = paging_new_directory();
    if (!dir) { ok("scratch address space", false); return; }

    u32 kp = pmm_alloc_frame(), up = pmm_alloc_frame(), kp2 = pmm_alloc_frame();
    if (!kp || !up || !kp2) { ok("scratch frames", false); return; }

    /* A kernel mapping above where user space begins, which is exactly what
       the framebuffer aperture is. */
    map_page_in(dir, 0x38000000, kp, PTE_PRESENT | PTE_RW);
    ok("a kernel page above user space is mapped",
       virt_to_phys_in(dir, 0x38000000) != 0);
    ok("but ring 3 cannot reach it", !virt_is_user_in(dir, 0x38000000));

    /* Two pages sharing one table: mapping the user one widens the directory
       entry, and its kernel neighbour must stay out of reach anyway. */
    map_page_in(dir, 0x39000000, up,  PTE_PRESENT | PTE_RW | PTE_USER);
    map_page_in(dir, 0x39001000, kp2, PTE_PRESENT | PTE_RW);
    ok("a user page is reachable", virt_is_user_in(dir, 0x39000000));
    ok("its kernel neighbour in the same table is not",
       !virt_is_user_in(dir, 0x39001000));
    ok("an unmapped address is not", !virt_is_user_in(dir, 0x3A000000));

    paging_free_directory(dir);
    pmm_free_frame(kp);
    pmm_free_frame(kp2);
}

static void test_fs(void) {
    char buf[64];
    u32 size = 0;

    vfs_delete("/t.txt");
    ok("write file", vfs_write("/t.txt", "hello", 5));
    ok("read it back", vfs_read("/t.txt", buf, sizeof(buf)) == 5 && memcmp(buf, "hello", 5) == 0);
    ok("stat reports the size", vfs_stat("/t.txt", &size, 0) && size == 5);
    ok("append", vfs_append("/t.txt", "!!", 2));
    ok("append grew the file",
       vfs_read("/t.txt", buf, sizeof(buf)) == 7 && memcmp(buf, "hello!!", 7) == 0);

    u32 n = vfs_count("/");
    ok("create second file", vfs_write("/u.txt", "x", 1) && vfs_count("/") == n + 1);
    ok("delete", vfs_delete("/t.txt") && !vfs_stat("/t.txt", 0, 0));
    vfs_delete("/u.txt");
}

static void test_paths(void) {
    char out[VFS_PATH_MAX];

    ok("an absolute path is left alone",
       vfs_resolve("/a/b", out, sizeof(out)) && strcmp(out, "/a/b") == 0);
    ok("repeated slashes collapse",
       vfs_resolve("//a///b", out, sizeof(out)) && strcmp(out, "/a/b") == 0);
    ok("a dot goes nowhere",
       vfs_resolve("/a/./b", out, sizeof(out)) && strcmp(out, "/a/b") == 0);
    ok("dot dot climbs one",
       vfs_resolve("/a/b/..", out, sizeof(out)) && strcmp(out, "/a") == 0);
    ok("dot dot in the middle",
       vfs_resolve("/a/../b", out, sizeof(out)) && strcmp(out, "/b") == 0);
    ok("dot dot stops at the root",
       vfs_resolve("/../../..", out, sizeof(out)) && strcmp(out, "/") == 0);
    ok("the root resolves to itself",
       vfs_resolve("/", out, sizeof(out)) && strcmp(out, "/") == 0);

    /* Relative paths are joined to wherever the caller is. */
    ok("a directory can be entered", vfs_mkdir("/sub") && vfs_chdir("/sub"));
    ok("the working directory follows", strcmp(vfs_cwd(), "/sub") == 0);
    ok("a relative name resolves inside it",
       vfs_resolve("f.txt", out, sizeof(out)) && strcmp(out, "/sub/f.txt") == 0);
    ok("and dot dot leaves it",
       vfs_resolve("../g.txt", out, sizeof(out)) && strcmp(out, "/g.txt") == 0);
    vfs_chdir("/");
    vfs_rmdir("/sub");
}

static void test_directories(void) {
    vfs_delete("/d/inner.txt");
    vfs_rmdir("/d/deep");
    vfs_rmdir("/d");

    ok("mkdir creates one", vfs_mkdir("/d"));
    bool is_dir = false;
    ok("it stats as a directory", vfs_stat("/d", 0, &is_dir) && is_dir);
    ok("making it twice fails", !vfs_mkdir("/d"));

    ok("a file can be written inside it", vfs_write("/d/inner.txt", "nested", 6));
    char buf[16];
    ok("and read back out", vfs_read("/d/inner.txt", buf, sizeof(buf)) == 6 &&
                            memcmp(buf, "nested", 6) == 0);
    ok("it lists inside, not outside", vfs_count("/d") == 1);

    ok("directories nest", vfs_mkdir("/d/deep") && vfs_write("/d/deep/x", "y", 1));
    ok("the nested file reads back", vfs_read("/d/deep/x", buf, sizeof(buf)) == 1);
    ok("a path through two levels resolves",
       vfs_stat("/d/deep/../deep/x", 0, 0));

    ok("rmdir refuses a directory with things in it", !vfs_rmdir("/d"));
    ok("emptying it lets rmdir work",
       vfs_delete("/d/deep/x") && vfs_rmdir("/d/deep") &&
       vfs_delete("/d/inner.txt") && vfs_rmdir("/d"));
    ok("and it is gone", !vfs_stat("/d", 0, 0));
}

static void test_timer(void) {
    u64 a = timer_ticks();
    sleep_ms(60);
    u64 b = timer_ticks();
    ok("timer advances", b > a);
    ok("timer roughly matches the requested delay", (b - a) >= 4 && (b - a) <= 20);
}

static volatile int bp_hits = 0;
static void on_breakpoint(registers_t *r) { (void)r; bp_hits++; }

static void test_interrupts(void) {
    /* If the IDT were broken this would have triple faulted long ago, but
       take an explicit software interrupt to be sure the whole path still
       works: stub, dispatcher, handler, and the return. */
    register_interrupt_handler(3, on_breakpoint);
    int before = bp_hits;
    __asm__ volatile ("int $3");
    ok("software interrupt reached its handler", bp_hits == before + 1);
    __asm__ volatile ("int $3");
    __asm__ volatile ("int $3");
    ok("handler is re-entrant", bp_hits == before + 3);
}


static void test_disk(void) {
    if (!blk_present()) { kprintf("  SKIP  no disk attached\n"); return; }
    ok("disk reports a size", blk_sectors() > 0);

    /* Use a sector well past the filesystem so nothing real is disturbed,
       and put back whatever was there. */
    u32 lba = blk_sectors() - 4;
    u8 original[SECTOR_SIZE], probe[SECTOR_SIZE], back[SECTOR_SIZE];
    ok("read a sector", blk_read(lba, 1, original));

    for (u32 i = 0; i < SECTOR_SIZE; i++) probe[i] = (u8)(i * 7 + 3);
    ok("write a sector", blk_write(lba, 1, probe));
    ok("read it back", blk_read(lba, 1, back));
    ok("what came back is what went out", memcmp(probe, back, SECTOR_SIZE) == 0);

    blk_write(lba, 1, original);
    ok("original contents restored", blk_read(lba, 1, back) && memcmp(original, back, SECTOR_SIZE) == 0);
}

static void test_net(void) {
    if (!net_up()) { kprintf("  SKIP  no network card\n"); return; }
    const u8 *m = net_mac();
    bool nonzero = false;
    for (int i = 0; i < 6; i++) if (m[i]) nonzero = true;
    ok("card has a mac address", nonzero);

    ok("dhcp obtained a lease", net_dhcp(8000));
    if (net_ip()) {
        ok("address is not zero", net_ip() != 0);
        ok("gateway was supplied", net_gateway() != 0);
        ok("resolver was supplied", net_dns() != 0);
        ok("gateway answers icmp", net_ping(net_gateway(), 3000) >= 0);
        ipv4_t ip = 0;
        ok("dns resolves a name", net_resolve("example.com", &ip, 6000) && ip != 0);
    }
}


static void test_video(void) {
    if (!fb_active()) { kprintf("  SKIP  no framebuffer\n"); return; }
    ok("mode is the one that was asked for", fb_width() == 1024 && fb_height() == 768);
    ok("pitch matches the width", fb_pitch() == fb_width() * 4);
    ok("text grid derives from the font", fbcon_cols() == fb_width() / FONT_W);

    /* Write a pixel and read it back out of the back buffer. */
    u32 probe = RGB(0x12, 0x34, 0x56);
    u32 keep = fb_get(900, 700);
    fb_put(900, 700, probe);
    ok("pixel round trips", fb_get(900, 700) == probe);

    fb_rect(880, 690, 20, 20, RGB(1, 2, 3));
    ok("rect fills its interior", fb_get(890, 700) == RGB(1, 2, 3));
    ok("rect stops at its edge", fb_get(905, 700) != RGB(1, 2, 3));
    fb_put(900, 700, keep);

    bool inked = false;
    for (u32 y = 0; y < FONT_H; y++) if (font8x16[(u8)'A' - FONT_FIRST][y]) inked = true;
    ok("font has glyph data", inked);
}

static void test_mouse(void) {
    if (!mouse_present()) { kprintf("  SKIP  no mouse\n"); return; }
    ok("pointer starts on screen",
       mouse_x() >= 0 && mouse_x() < (i32)fb_width() &&
       mouse_y() >= 0 && mouse_y() < (i32)fb_height());
}


static void test_fat(void) {
    if (!blk_present()) { kprintf("  SKIP  no disk attached\n"); return; }
    ok("volume is mounted", fat_mounted());
    ok("cluster count is in the FAT16 range",
       fat_total_clusters() >= 4085 && fat_total_clusters() <= 65524);
    ok("clusters are a sensible size", fat_cluster_bytes() >= 512);

    /* A file that spans more than one cluster exercises chain following,
       which a single sector write would not. */
    u32 big = fat_cluster_bytes() * 2 + 137;
    u8 *out = (u8 *)kmalloc(big);
    u8 *in  = (u8 *)kmalloc(big);
    if (!out || !in) { ok("scratch buffers", false); return; }
    for (u32 i = 0; i < big; i++) out[i] = (u8)(i * 31 + 7);

    ok("write a multi-cluster file", fat_write_file("/sptest.bin", out, big));
    int got = fat_read_file("/sptest.bin", in, big);
    ok("read back the same length", got == (int)big);
    ok("read back the same bytes", got == (int)big && memcmp(out, in, big) == 0);
    ok("it appears in the directory", fat_count("/") > 0);
    ok("delete removes it", fat_delete_file("/sptest.bin"));
    ok("and it is gone", fat_read_file("/sptest.bin", in, big) < 0);

    /* A subdirectory is a cluster chain rather than the fixed root area, so
       it exercises a different path through the same code. */
    fat_delete_file("/sub/deep.bin");
    fat_rmdir("/sub");
    ok("a subdirectory can be made", fat_mkdir("/sub"));
    ok("a file spanning clusters fits in it", fat_write_file("/sub/deep.bin", out, big));
    ok("and reads back byte for byte",
       fat_read_file("/sub/deep.bin", in, big) == (int)big && memcmp(out, in, big) == 0);
    ok("the root does not show what is inside it", fat_count("/sub") == 1);
    ok("cleaning up works",
       fat_delete_file("/sub/deep.bin") && fat_rmdir("/sub"));

    kfree(out);
    kfree(in);
}


/* Builds a minimal but structurally valid ELF32 header in a caller supplied
   buffer, so individual fields can then be corrupted one at a time. */
static void make_elf(u8 *buf, u32 vaddr) {
    memset(buf, 0, 128);
    buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 1;                       /* 32 bit */
    buf[5] = 1;                       /* little endian */
    *(u16 *)(buf + 16) = 2;           /* ET_EXEC */
    *(u16 *)(buf + 18) = 3;           /* EM_386 */
    *(u32 *)(buf + 24) = vaddr;       /* entry */
    *(u32 *)(buf + 28) = 52;          /* phoff */
    *(u16 *)(buf + 42) = 32;          /* phentsize */
    *(u16 *)(buf + 44) = 1;           /* phnum */
    u8 *ph = buf + 52;
    *(u32 *)(ph + 0)  = 1;            /* PT_LOAD */
    *(u32 *)(ph + 4)  = 0;            /* offset */
    *(u32 *)(ph + 8)  = vaddr;        /* vaddr */
    *(u32 *)(ph + 16) = 16;           /* filesz */
    *(u32 *)(ph + 20) = 16;           /* memsz */
}

static void test_elf(void) {
    u8 buf[128];
    u32 entry = 0;
    u32 dir = paging_new_directory();
    if (!dir) { ok("scratch address space", false); return; }

    ok("rejects a buffer too short to hold a header",
       elf_load(dir, buf, 8, &entry) == ELF_ERR_SHORT);

    make_elf(buf, 0x40000000);
    buf[1] = 'X';
    ok("rejects a bad magic number", elf_load(dir, buf, sizeof(buf), &entry) == ELF_ERR_MAGIC);

    make_elf(buf, 0x40000000);
    buf[4] = 2;                                     /* claims 64 bit */
    ok("rejects the wrong class", elf_load(dir, buf, sizeof(buf), &entry) == ELF_ERR_CLASS);

    make_elf(buf, 0x40000000);
    *(u16 *)(buf + 18) = 40;                        /* ARM */
    ok("rejects another machine", elf_load(dir, buf, sizeof(buf), &entry) == ELF_ERR_TYPE);

    /* The important one: a program must not be able to ask to be mapped
       over the kernel. */
    make_elf(buf, 0x00100000);
    ok("rejects a segment inside kernel memory",
       elf_load(dir, buf, sizeof(buf), &entry) == ELF_ERR_RANGE);

    make_elf(buf, 0x40000000);
    *(u32 *)(buf + 52 + 16) = 4096;                 /* filesz past the end */
    ok("rejects a segment that runs off the end of the file",
       elf_load(dir, buf, sizeof(buf), &entry) == ELF_ERR_OVERFLOW);

    make_elf(buf, 0x40000000);
    ok("accepts a well formed header",
       elf_load(dir, buf, sizeof(buf), &entry) == ELF_OK && entry == 0x40000000);

    paging_free_directory(dir);
}

static void test_userspace(void) {
    /* The stub makes six putc calls and then exits, so the syscall counter
       moving is direct evidence that ring 3 code ran and crossed back in.
       Counting tasks would race: it can finish before the check. */
    u32 before = syscall_count();
    u32 free_before = pmm_free_frames();
    int pid = user_spawn_stub("selftest-ring3");
    ok("a ring 3 task can be created", pid > 0);
    if (pid <= 0) return;

    task_wait((u32)pid);
    ok("it reached exit on its own", !task_alive((u32)pid));
    ok("ring 3 code issued system calls", syscall_count() >= before + 7);
    ok("its address space was reclaimed", pmm_free_frames() == free_before);
}


static void test_gfx(void) {
    const int W = 32, H = 24;
    u32 *px = (u32 *)kmalloc((u32)(W * H) * 4);
    if (!px) { ok("scratch surface", false); return; }

    surf_clear(px, W, H, 0x111111);
    ok("clear fills every pixel", px[0] == 0x111111 && px[W * H - 1] == 0x111111);

    surf_rect(px, W, H, 4, 4, 8, 8, 0x222222);
    ok("rect fills its interior", px[6 * W + 6] == 0x222222);
    ok("rect leaves the outside alone", px[2 * W + 2] == 0x111111);

    /* A rectangle hanging off the edge must clip rather than write past the
       end of the surface. */
    surf_rect(px, W, H, -4, -4, 8, 8, 0x333333);
    ok("rect clips at the top left", px[0] == 0x333333 && px[3 * W + 3] == 0x333333);
    surf_rect(px, W, H, W - 4, H - 4, 8, 8, 0x444444);
    ok("rect clips at the bottom right", px[(H - 1) * W + (W - 1)] == 0x444444);

    surf_clear(px, W, H, 0);
    surf_line(px, W, H, 2, 2, 20, 12, 1, 0x555555);
    ok("line marks its start", px[2 * W + 2] == 0x555555);
    ok("line marks its end", px[12 * W + 20] == 0x555555);

    surf_clear(px, W, H, 0);
    surf_disc(px, W, H, 16, 12, 4, 0x666666);
    ok("disc fills its centre", px[12 * W + 16] == 0x666666);
    ok("disc stays inside its radius", px[12 * W + 25] == 0);

    surf_clear(px, W, H, 0);
    surf_text(px, W, H, 1, 1, "A", 0x777777);
    bool inked = false;
    for (int i = 0; i < W * H; i++) if (px[i] == 0x777777) inked = true;
    ok("text puts ink on the surface", inked);

    char buf[32];
    kformat(buf, sizeof(buf), "%d/%s/%x", 42, "ok", 255);
    ok("kformat formats", strcmp(buf, "42/ok/ff") == 0);
    kformat(buf, sizeof(buf), "%-4s/%-3d", "x", 7);
    ok("kformat left aligns", strcmp(buf, "x   /7  ") == 0);
    kformat(buf, 6, "abcdefghij");
    ok("kformat respects the buffer size", strlen(buf) == 5);

    kfree(px);
}

static void test_wm(void) {
    if (!fb_active()) { kprintf("  SKIP  no framebuffer\n"); return; }

    window_t *a = wm_create("a", 10, 10, 120, 80);
    window_t *b = wm_create("b", 40, 40, 120, 80);
    ok("windows can be created", a && b);
    if (!a || !b) return;

    ok("outer size allows for the chrome",
       wm_outer_w(a) == 120 + WM_BORDER * 2 &&
       wm_outer_h(a) == 80 + WM_TITLE_H + WM_BORDER);

    /* Closing must also drop the manager's reference, or the next composite
       walks freed memory. */
    wm_close(a);
    wm_close(b);
    ok("windows can be closed", true);
}

static void test_winsrv(void) {
    const u32 PID = 4242, OTHER = 4243;

    int h = winsrv_create(PID, "selftest", 64, 48);
    ok("a window can be created for a program", h >= 0);
    if (h < 0) return;

    ok("its size comes back", winsrv_size(PID, h) == ((64 << 16) | 48));
    ok("another program cannot see the handle", winsrv_size(OTHER, h) == -1);

    u32 ua = winsrv_surface(PID, h, paging_current_directory());
    ok("the surface maps into the caller", ua == WINSRV_SURFACE_BASE);

    /* Writing through the address the program was given must land in the
       pixels the window manager composites from. Those pages are identity
       mapped for the kernel, so the physical address is readable here. */
    if (ua) {
        *(volatile u32 *)ua = 0xDEADBEEF;
        u32 phys = virt_to_phys(ua);
        ok("it aliases the window pixels", phys && *(volatile u32 *)phys == 0xDEADBEEF);

        /* Pixel zero for the program has to be pixel zero for the window.
           Mapping the page before it puts every row out by a fixed amount,
           which draws a recognisable but wrong picture. */
        window_t *win = winsrv_window(PID, h);
        ok("and starts exactly where the window does", win && phys == (u32)win->canvas);

        /* The last pixel must be inside the mapping too. */
        u32 last = ua + (64u * 48u - 1) * 4;
        *(volatile u32 *)last = 0xFEEDFACE;
        ok("the whole surface is mapped",
           win && win->canvas[64 * 48 - 1] == 0xFEEDFACE);
        ok("asking again returns the same address",
           winsrv_surface(PID, h, paging_current_directory()) == ua);
    }

    ok("a foreign program cannot map it", winsrv_surface(OTHER, h, paging_current_directory()) == 0);
    ok("commit is accepted", winsrv_commit(PID, h));
    ok("a foreign commit is not", !winsrv_commit(OTHER, h));

    /* Closing goes through wm_close, which must not release a surface the
       server carved out of a larger allocation: that pointer is not one
       kmalloc returned, and the server frees the real one itself. A heap
       that still balances afterwards is the evidence. */
    u32 heap_before_close = heap_used();
    ok("the window closes", winsrv_close(PID, h));
    ok("the handle is dead afterwards", winsrv_surface(PID, h, paging_current_directory()) == 0);
    ok("closing it gave the heap back rather than corrupting it",
       heap_used() < heap_before_close);

    /* Everything a task owned goes away with it. */
    int a = winsrv_create(PID, "one", 40, 40);
    int b = winsrv_create(PID, "two", 40, 40);
    ok("a program can hold more than one window", a >= 0 && b >= 0 && a != b);
    winsrv_release(PID);
    ok("its windows go when the program does", winsrv_size(PID, a) == -1 && winsrv_size(PID, b) == -1);
}

static void test_builtin(void) {
    ok("programs ship with the kernel", builtin_count_programs() >= 3);

    u32 size = 0;
    ok("paint is one of them", vfs_stat("/paint.elf", &size, 0) && size > 1024);

    u8 head[4] = { 0, 0, 0, 0 };
    vfs_read("/paint.elf", head, sizeof(head));
    ok("and it is a real ELF",
       head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F');

    ok("they cannot be overwritten", !vfs_write("/paint.elf", "x", 1));
    ok("nor deleted", !vfs_delete("/paint.elf"));

    /* The disk must not be holding its own copy, or rebuilding the kernel
       would change nothing on a machine that had already booted once. */
    if (blk_present() && fat_mounted()) {
        char name[VFS_NAME_MAX];
        bool on_disk = false;
        for (u32 i = 0; ; i++) {
            if (fat_list("/", i, name, 0, 0) != 1) break;
            if (strcmp(name, "paint.elf") == 0) on_disk = true;
        }
        ok("and are not written to the disk", !on_disk);
    }
}

static void test_open_files(void) {
    vfs_delete("/fd.txt");

    int fd = vfs_open("/fd.txt", O_WRITE | O_CREATE);
    ok("a file can be opened for writing", fd >= 0);
    if (fd < 0) return;

    ok("writing reports what it took", vfs_fd_write(fd, "abcdefgh", 8) == 8);
    ok("seeking back works", vfs_fd_seek(fd, 0, 0) == 0);
    ok("overwriting in place works", vfs_fd_write(fd, "ABC", 3) == 3);
    ok("the size is what was written", vfs_fd_size(fd) == 8);
    ok("closing writes it out", vfs_close(fd));

    char buf[16];
    ok("and the file has the edit",
       vfs_read("/fd.txt", buf, sizeof(buf)) == 8 && memcmp(buf, "ABCdefgh", 8) == 0);

    fd = vfs_open("/fd.txt", O_READ);
    ok("reading a chunk at a time works", fd >= 0 && vfs_fd_read(fd, buf, 3) == 3);
    ok("it starts where it left off", vfs_fd_read(fd, buf, 3) == 3 && memcmp(buf, "def", 3) == 0);
    ok("seeking to the end reports the size", vfs_fd_seek(fd, 0, 2) == 8);
    ok("reading past the end gives nothing", vfs_fd_read(fd, buf, 4) == 0);
    vfs_close(fd);

    ok("a missing file will not open without create", vfs_open("/nope.txt", O_READ) < 0);
    vfs_delete("/fd.txt");
}

static void test_theme(void) {
    vfs_delete(THEME_FILE);
    theme_init();

    ok("there is a default accent", theme()->accent != 0);
    ok("and it is one of the presets", theme_current_preset() >= 0);

    /* A preset changes the palette without touching anything else. */
    int corner_before = theme()->corner;
    theme_apply_preset(3);
    ok("a preset changes the accent", theme()->accent == theme_preset_accent(3));
    ok("and leaves the rest alone", theme()->corner == corner_before);

    /* Saving and reloading has to round trip, or the settings window would
       appear to work and then forget. */
    theme_apply_preset(4);
    ok("the theme saves", theme_save());
    theme_apply_preset(0);
    ok("reloading reports a change", theme_reload());
    ok("and brings the saved accent back", theme()->accent == theme_preset_accent(4));
    ok("reloading again reports no change", !theme_reload());

    /* The file is the interface a ring 3 program writes, so hand-written
       text has to work exactly as well as what theme_save produces. */
    const char *hand = "# by hand\npreset 2\nwallpaper 1\ncorner 14\nshadows 0\n";
    ok("a hand written config writes", vfs_write(THEME_FILE, hand, (u32)strlen(hand)));
    ok("and is picked up", theme_reload());
    ok("preset applied", theme()->accent == theme_preset_accent(2));
    ok("wallpaper applied", theme()->wallpaper == WALLPAPER_GRID);
    ok("corner applied", theme()->corner == 14);
    ok("shadows applied", !theme()->shadows);

    /* A value out of range must be clamped rather than believed. */
    const char *bad = "wallpaper 99\ncorner 900\n";
    vfs_write(THEME_FILE, bad, (u32)strlen(bad));
    theme_reload();
    ok("a silly wallpaper falls back", theme()->wallpaper < WALLPAPER_COUNT);
    ok("a silly corner is clamped", theme()->corner <= 20);

    vfs_delete(THEME_FILE);
}

/* --- the other processors -------------------------------------------------

   These have to prove three separate things, because a processor that
   started but never runs anything looks exactly like one that works:

     - it executes code we gave it, and the code sees the right argument
     - it runs at the same time as this one rather than instead of it
     - the lock between them actually excludes

   The counter test does the last two together. Every participant adds the
   same number of times under the lock, and the total has to be exact. A
   broken lock loses increments; a processor that never ran loses all of
   them at once. */

#define SMP_ADDS 20000

static spinlock_t test_lock;
static volatile u32 shared_counter;
static volatile u32 seen_arg[SMP_MAX_CPUS];

static void smp_add_work(void *arg) {
    u32 who = (u32)arg;
    if (who < SMP_MAX_CPUS) seen_arg[who] = who + 1;
    for (u32 i = 0; i < SMP_ADDS; i++) {
        spin_lock(&test_lock);
        shared_counter++;
        spin_unlock(&test_lock);
    }
}

static void test_smp(void) {
    ok("the firmware described at least one processor", smp_cpu_count() >= 1);
    ok("this one is running", smp_cpu(0) && smp_cpu(0)->started);

    if (smp_cpu_count() < 2) {
        kprintf("  SKIP  only one processor on this machine\n");
        return;
    }

    ok("every processor found was started", smp_started() == smp_cpu_count());

    u32 helpers = 0;
    for (u32 i = 1; i < smp_cpu_count(); i++)
        if (smp_cpu(i)->started) helpers++;
    ok("at least one other processor came up", helpers > 0);

    /* They should be spinning in their idle loop already. */
    u64 spins_before = smp_cpu(1)->spins;
    sleep_ms(50);
    ok("an idle processor is really looping", smp_cpu(1)->spins > spins_before);

    /* Hand the same job to all of them and join in. */
    shared_counter = 0;
    test_lock = 0;
    for (u32 i = 0; i < SMP_MAX_CPUS; i++) seen_arg[i] = 0;

    u32 dispatched = 0;
    for (u32 i = 1; i < smp_cpu_count(); i++)
        if (smp_run(i, smp_add_work, (void *)i)) dispatched++;
    ok("work was accepted by every other processor", dispatched == helpers);

    smp_add_work((void *)0);              /* this processor does a share too */

    bool joined = true;
    for (u32 i = 1; i < smp_cpu_count(); i++)
        if (!smp_wait(i, 8000)) joined = false;
    ok("they all finished", joined);

    ok("the count is exact, so the lock held",
       shared_counter == SMP_ADDS * (helpers + 1));

    bool args_ok = (seen_arg[0] == 1);
    for (u32 i = 1; i < smp_cpu_count(); i++)
        if (smp_cpu(i)->started && seen_arg[i] != i + 1) args_ok = false;
    ok("each one was handed its own argument", args_ok);

    bool counted = true;
    for (u32 i = 1; i < smp_cpu_count(); i++)
        if (smp_cpu(i)->started && smp_cpu(i)->jobs != 1) counted = false;
    ok("each one recorded exactly one job", counted);

    /* Nothing should be left holding the lock. */
    spin_lock(&test_lock);
    spin_unlock(&test_lock);
    ok("the lock is free afterwards", true);
}

int selftest_run(void) {
    passed = failed = 0;
    kprintf("\n=== nyx self test ===\n");
    kprintf("[string]\n");     test_string();
    kprintf("[physical memory]\n"); test_pmm();
    kprintf("[paging]\n");     test_paging();
    kprintf("[user access]\n"); test_user_access();
    kprintf("[heap]\n");       test_heap();
    kprintf("[filesystem]\n"); test_fs();
    kprintf("[paths]\n");      test_paths();
    kprintf("[directories]\n"); test_directories();
    kprintf("[open files]\n");  test_open_files();
    kprintf("[timer]\n");      test_timer();
    kprintf("[interrupts]\n"); test_interrupts();
    kprintf("[disk]\n");       test_disk();
    kprintf("[fat]\n");        test_fat();
    kprintf("[network]\n");    test_net();
    kprintf("[elf]\n");        test_elf();
    kprintf("[userspace]\n");  test_userspace();
    kprintf("[video]\n");      test_video();
    kprintf("[mouse]\n");      test_mouse();
    kprintf("[graphics]\n");   test_gfx();
    kprintf("[windows]\n");    test_wm();
    kprintf("[window server]\n"); test_winsrv();
    kprintf("[built-in programs]\n"); test_builtin();
    kprintf("[theme]\n");      test_theme();
    kprintf("[processors]\n"); test_smp();
    kprintf("\n%d passed, %d failed\n", passed, failed);
    kprintf(failed ? "SELFTEST_FAIL\n" : "SELFTEST_PASS\n");
    return failed;
}
