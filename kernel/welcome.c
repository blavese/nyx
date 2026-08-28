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
    kprintf("  1. Files\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     ls                       see what is stored\n");
    kprintf("     cat readme.txt           read a file\n");
    kprintf("     write notes.txt hello    make one\n");
    kprintf("     rm notes.txt             delete it\n");
    kprintf("     Files live in memory, so they vanish on reboot.\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  2. Running programs at the same time\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     ps                       list what is running\n");
    kprintf("     spawn                    start another task\n");
    kprintf("     ps                       watch it appear\n");
    kprintf("     A timer interrupts 100 times a second and swaps\n");
    kprintf("     between tasks. That is how one CPU does many things.\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  3. The machine itself\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     mem                      memory the kernel found and used\n");
    kprintf("     uptime                   how long since boot\n");
    kprintf("     uname                    what this is\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  4. Crashing it on purpose\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     fault                    divide by zero\n");
    kprintf("     The CPU catches it and the kernel prints where it\n");
    kprintf("     happened, rather than the machine just stopping.\n");
    kprintf("     You will need to close the window afterwards.\n\n");

    vga_set_color(VGA_LGREEN, VGA_BLACK);
    kprintf("  Nothing here can harm your real computer. This is running\n");
    kprintf("  inside an emulator, in its own pretend machine.\n\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
}
