#include "types.h"
#include "vga.h"
#include "serial.h"
#include "printf.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "keyboard.h"
#include "multiboot.h"
#include "handoff.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "sched.h"
#include "fs.h"
#include "blockdev.h"
#include "diskfs.h"
#include "pci.h"
#include "netdev.h"
#include "net.h"
#include "fb.h"
#include "fbcon.h"
#include "mouse.h"
#include "syscall.h"
#include "shell.h"
#include "winsrv.h"
#include "smp.h"
#include "acpi.h"
#include "vfs.h"
#include "builtin.h"
#include "selftest.h"
#include "string.h"
#include "io.h"

#define HEAP_BASE (8u * 1024 * 1024)
#define HEAP_SIZE (16u * 1024 * 1024)

static bool want_selftest = false;

/* QEMU's isa-debug-exit device: writing here ends the VM with (code<<1)|1,
   which is how the test script gets a real exit status out of the kernel. */
static void machine_exit(u32 code) {
    outl(0xF4, code);
    for (;;) hlt();
}

static void banner(void) {
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("\n  +--------------------------------+\n");
    kprintf("  |  %s %-25s|\n", KERNEL_NAME, KERNEL_VERSION);
    kprintf("  |  x86-64 long mode              |\n");
    kprintf("  +--------------------------------+\n\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
}

static void selftest_task(void) {
    int failures = selftest_run();
    machine_exit(failures ? 2 : 0);
}

static void init_task(void) {
    /* Seeded once. Anything the user has since edited or deleted stays that
       way, because writing these back every boot would undo their work. */
    if (!vfs_stat("/readme.txt", 0, 0))
        vfs_write("/readme.txt",
                  "nyx is a small operating system written from scratch.\n"
                  "It boots via multiboot, manages its own memory, and\n"
                  "preempts its own tasks. Try: ls, cat, ps, mem, help\n", 158);
    if (!vfs_stat("/hello.txt", 0, 0))
        vfs_write("/hello.txt", "hello from a filesystem that lives on disk\n", 43);

    shell_task();
}

/* Where a multiboot loader arrives, once boot.S has put the processor into
   long mode. It has no handoff structure of its own, so one is built out of
   what it left behind and the machine carries on through the same door as
   everything else.

   The handoff lives here rather than on the stack because the stack this is
   called on belongs to boot.S and is not very large. */
static handoff_t multiboot_handoff;

void kmain(handoff_t *h);

void kmain_multiboot(u32 magic, u32 mbi_addr) {
    handoff_t *h = &multiboot_handoff;
    memset(h, 0, sizeof(*h));
    h->magic = HANDOFF_MAGIC;
    strncpy(h->loader, "multiboot", sizeof(h->loader) - 1);

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        /* Nothing has been initialised yet, so say it the only way there is
           and stop. */
        serial_init();
        panic("not booted by a multiboot loader (magic=%x)", magic);
    }

    multiboot_info_t *mbi = (multiboot_info_t *)(u64)mbi_addr;

    if ((mbi->flags & (1 << 2)) && mbi->cmdline)
        strncpy(h->cmdline, (const char *)(u64)mbi->cmdline, sizeof(h->cmdline) - 1);

    /* Multiboot describes memory in its own format; boil it down to the
       three kinds the kernel understands. */
    if (mbi->flags & (1 << 6)) {
        u64 p = mbi->mmap_addr;
        u64 end = (u64)mbi->mmap_addr + mbi->mmap_length;
        while (p < end && h->region_count < HANDOFF_MAX_REGIONS) {
            mb_mmap_entry_t *e = (mb_mmap_entry_t *)p;
            mem_region_t *r = &h->regions[h->region_count++];
            r->base = e->addr;
            r->len = e->len;
            r->type = (e->type == 1) ? MEM_USABLE : MEM_RESERVED;
            r->pad = 0;
            p += e->size + 4;
        }
    }

    if (!h->region_count) {
        /* A loader that described nothing. Assume what the fallback used to:
           everything above the first megabyte, up to what it claimed. */
        mem_region_t *r = &h->regions[h->region_count++];
        r->base = 0x100000;
        r->len = ((u64)mbi->mem_upper + 1024) * 1024;
        r->type = MEM_USABLE;
        r->pad = 0;
    }

    /* No framebuffer: fb_init sets a mode itself through the VBE ports,
       which only exists on a machine that has a BIOS. */
    h->fb_base = 0;

    kmain(h);
}

void kmain(handoff_t *h) {
    serial_init();
    vga_init();

    if (!h || h->magic != HANDOFF_MAGIC)
        panic("started without a handoff structure");

    for (const char *p = h->cmdline; *p; p++)
        if (!strncmp(p, "selftest", 8)) { want_selftest = true; break; }

    banner();

    gdt_init();      kprintf("  gdt     flat segments, tss installed\n");
    idt_init();      kprintf("  idt     256 vectors\n");
    pic_init();      kprintf("  pic     irqs remapped to 32..47\n");
    pmm_init(h);     kprintf("  memory  %d KiB usable, via %s\n",
                             (u32)(pmm_free_frames() * 4), h->loader);
    /* The heap lives in identity mapped memory, so the frame allocator
       has to be told about it or it will hand the same pages out twice. */
    pmm_reserve(HEAP_BASE, HEAP_SIZE);

    paging_init();   kprintf("  paging  enabled\n");
    heap_init(HEAP_BASE, HEAP_SIZE);
    kprintf("  heap    %d KiB\n", HEAP_SIZE / 1024);
    /* Needs paging to map the aperture and the heap for the back
       buffer, so this is the earliest it can come up. Anything
       printed before now is only in the serial log. */
    if (fb_init(1024, 768)) {
        fbcon_init();
        vga_set_color(VGA_LCYAN, VGA_BLACK);
        kprintf("  %s %s\n", KERNEL_NAME, KERNEL_VERSION);
        vga_set_color(VGA_LGREY, VGA_BLACK);
        kprintf("  video   %dx%d 32bpp, %dx%d text\n",
                fb_width(), fb_height(), fbcon_cols(), fbcon_rows());
    } else {
        kprintf("  video   no vbe, vga text mode\n");
    }
    fs_init();
    vfs_init();
    if (blk_init()) {
        kprintf("  disk    %s via %s, %d MiB\n", blk_model(), blk_driver(), blk_sectors() / 2048);
        int n = diskfs_mount();
        if (n >= 0)      kprintf("  fs      fat16 mounted, %d entries in the root\n", n);
        else if (n == -2) {
            /* A brand new disk should just work rather than telling
               someone to run a command they have never heard of. */
            if (diskfs_format()) kprintf("  fs      new disk prepared\n");
            else                 kprintf("  fs      could not prepare the disk\n");
        }
        else              kprintf("  fs      disk unreadable, using memory only\n");
    } else {
        kprintf("  disk    none, files will not persist\n");
    }
    builtin_install();
    kprintf("  progs   %d built in\n", builtin_count_programs());

    timer_init(100); kprintf("  timer   100 Hz\n");

    /* Needs the timer: the startup sequence is defined in microseconds and
       there is nothing to measure them with before it. */
    smp_init();
    if (smp_cpu_count() > 1)
        kprintf("  cpu     %d processors, %d started\n",
                smp_cpu_count(), smp_started());
    else
        kprintf("  cpu     1 processor\n");
    if (netdev_init()) {
        net_init();
        const u8 *m = net_mac();
        kprintf("  net     %s %02x:%02x:%02x:%02x:%02x:%02x\n",
                netdev_name(), m[0], m[1], m[2], m[3], m[4], m[5]);
    } else {
        kprintf("  net     no card found\n");
    }
    keyboard_init();
    if (fb_active() && mouse_init())
        kprintf("  mouse   ps/2, pointer at %d,%d\n", mouse_x(), mouse_y());
    serial_enable_irq();
    kprintf("  input   ps/2 keyboard + serial (irq driven)\n");

    syscall_init();
    winsrv_init();
    sched_init();
    if (want_selftest) task_create("selftest", selftest_task);
    else               task_create("init", init_task);

    kprintf("  sched   %d task(s)\n", task_count());
    sched_start();
}
