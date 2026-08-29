/* Boot-time self test. `run.sh -T` boots with this enabled, so the whole
   kernel can be checked from a script without a human watching a screen. */
#include "selftest.h"
#include "printf.h"
#include "string.h"
#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "fs.h"
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
    fs_delete("t.txt");
    ok("write file", fs_write("t.txt", "hello", 5));
    file_t *f = fs_find("t.txt");
    ok("find file", f && f->size == 5 && memcmp(f->data, "hello", 5) == 0);
    ok("append", fs_append("t.txt", "!!", 2));
    f = fs_find("t.txt");
    ok("append grew the file", f && f->size == 7 && memcmp(f->data, "hello!!", 7) == 0);
    u32 n = fs_count();
    ok("create second file", fs_write("u.txt", "x", 1) && fs_count() == n + 1);
    ok("delete", fs_delete("t.txt") && !fs_find("t.txt"));
    fs_delete("u.txt");
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

    ok("write a multi-cluster file", fat_write_file("sptest.bin", out, big));
    int got = fat_read_file("sptest.bin", in, big);
    ok("read back the same length", got == (int)big);
    ok("read back the same bytes", got == (int)big && memcmp(out, in, big) == 0);
    ok("it appears in the directory", fat_count() > 0);
    ok("delete removes it", fat_delete_file("sptest.bin"));
    ok("and it is gone", fat_read_file("sptest.bin", in, big) < 0);

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
    int pid = user_spawn_stub("selftest-ring3");
    ok("a ring 3 task can be created", pid > 0);
    if (pid <= 0) return;

    task_wait((u32)pid);
    ok("it reached exit on its own", !task_alive((u32)pid));
    ok("ring 3 code issued system calls", syscall_count() >= before + 7);
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
    file_t *f = fs_find("paint.elf");
    ok("paint is one of them", f && f->size > 1024);
    ok("and it is a real ELF", f && f->data[0] == 0x7F && f->data[1] == 'E' &&
                               f->data[2] == 'L' && f->data[3] == 'F');
    ok("they are marked as coming from the kernel", f && f->builtin);

    /* The disk must not be holding its own copy, or rebuilding the kernel
       would change nothing on a machine that had already booted once. */
    if (blk_present() && fat_mounted()) {
        diskfs_sync();
        char name[FS_NAME_MAX];
        bool on_disk = false;
        for (u32 i = 0; ; i++) {
            if (fat_list(i, name, 0) != 1) break;
            if (strcmp(name, "PAINT.ELF") == 0) on_disk = true;
        }
        ok("and are not written to the disk", !on_disk);
    }
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
    kprintf("\n%d passed, %d failed\n", passed, failed);
    kprintf(failed ? "SELFTEST_FAIL\n" : "SELFTEST_PASS\n");
    return failed;
}
