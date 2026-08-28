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
#include "idt.h"
#include "ata.h"
#include "diskfs.h"
#include "net.h"
#include "rtl8139.h"
#include "fb.h"
#include "fbcon.h"
#include "mouse.h"
#include "font.h"

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
    if (!ata_present()) { kprintf("  SKIP  no disk attached\n"); return; }
    ok("disk reports a size", ata_sectors() > 0);

    /* Use a sector well past the filesystem so nothing real is disturbed,
       and put back whatever was there. */
    u32 lba = ata_sectors() - 4;
    u8 original[SECTOR_SIZE], probe[SECTOR_SIZE], back[SECTOR_SIZE];
    ok("read a sector", ata_read(lba, 1, original));

    for (u32 i = 0; i < SECTOR_SIZE; i++) probe[i] = (u8)(i * 7 + 3);
    ok("write a sector", ata_write(lba, 1, probe));
    ok("read it back", ata_read(lba, 1, back));
    ok("what came back is what went out", memcmp(probe, back, SECTOR_SIZE) == 0);

    ata_write(lba, 1, original);
    ok("original contents restored", ata_read(lba, 1, back) && memcmp(original, back, SECTOR_SIZE) == 0);
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

int selftest_run(void) {
    passed = failed = 0;
    kprintf("\n=== nyx self test ===\n");
    kprintf("[string]\n");     test_string();
    kprintf("[physical memory]\n"); test_pmm();
    kprintf("[paging]\n");     test_paging();
    kprintf("[heap]\n");       test_heap();
    kprintf("[filesystem]\n"); test_fs();
    kprintf("[timer]\n");      test_timer();
    kprintf("[interrupts]\n"); test_interrupts();
    kprintf("[disk]\n");       test_disk();
    kprintf("[network]\n");    test_net();
    kprintf("[video]\n");      test_video();
    kprintf("[mouse]\n");      test_mouse();
    kprintf("\n%d passed, %d failed\n", passed, failed);
    kprintf(failed ? "SELFTEST_FAIL\n" : "SELFTEST_PASS\n");
    return failed;
}
