/* First-run text. Someone who has never seen a kernel prompt before should
   be able to work out what to do from here without reading the source. */
#include "printf.h"
#include "vga.h"

void welcome_print(void) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("\n  Welcome. You are talking to an operating system\n");
    kprintf("  that was written from scratch.\n\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("  There is no Windows underneath this. The machine booted\n");
    kprintf("  straight into this kernel, and everything on screen is\n");
    kprintf("  being drawn by it.\n\n");
    vga_set_color(VGA_LGREEN, VGA_BLACK);
    kprintf("  Try typing:  guide\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("  Or:          help    for the full command list\n\n");
}

void guide_print(void) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("\n  A short tour\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("  ------------\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  1. Files that stay put\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     ls                       see what is stored\n");
    kprintf("     cat readme.txt           read a file\n");
    kprintf("     write notes.txt hello    make one\n");
    kprintf("     disk                     the drive it is all kept on\n");
    kprintf("     Files are written to a real disk. Close this window,\n");
    kprintf("     start it again, and they will still be here.\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  2. The internet\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     dhcp                     ask the network for an address\n");
    kprintf("     net                      show what it got\n");
    kprintf("     ping 10.0.2.2            talk to the gateway\n");
    kprintf("     resolve example.com      look up a real name\n");
    kprintf("     fetch example.com / page.html\n");
    kprintf("     That last one downloads a live web page and saves it.\n");
    kprintf("     Then: cat page.html\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  3. Running real programs\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     exec hello.elf           run a program and wait\n");
    kprintf("     bg count.elf             run one in the background\n");
    kprintf("     ps                       see what is running\n");
    kprintf("     These are separate executables, compiled on their\n");
    kprintf("     own and loaded from the disk. They run in ring 3,\n");
    kprintf("     where they cannot touch the kernel, and ask for\n");
    kprintf("     everything through system calls.\n\n");
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  4. The machine itself\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     mem                      memory found and used\n");
    kprintf("     uptime                   how long since boot\n");
    kprintf("     mouse                    where the pointer is\n");
    kprintf("     fault                    divide by zero on purpose\n");
    kprintf("     The CPU catches that last one and the kernel prints\n");
    kprintf("     where it happened rather than just stopping.\n\n");

    vga_set_color(VGA_LGREEN, VGA_BLACK);
    kprintf("  None of this can harm your real computer. It runs inside\n");
    kprintf("  an emulator, on its own pretend machine and its own disk.\n\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
}
