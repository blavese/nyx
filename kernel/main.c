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
    kprintf("  |  i686 protected mode           |\n");
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

void kmain(u32 magic, u32 mbi_addr) {
    serial_init();
    vga_init();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a multiboot loader (magic=%x)", magic);
    multiboot_info_t *mbi = (multiboot_info_t *)mbi_addr;

    if ((mbi->flags & (1 << 2)) && mbi->cmdline) {
        const char *cmd = (const char *)mbi->cmdline;
        for (const char *p = cmd; *p; p++)
            if (!strncmp(p, "selftest", 8)) { want_selftest = true; break; }
    }

    banner();

    gdt_init();      kprintf("  gdt     flat segments, tss installed\n");
    idt_init();      kprintf("  idt     256 vectors\n");
    pic_init();      kprintf("  pic     irqs remapped to 32..47\n");
    pmm_init(mbi);   kprintf("  memory  %d KiB usable\n", pmm_free_frames() * 4);
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
