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
    kprintf("  1. The desktop\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     desktop                  windows, a mouse, a terminal\n");
    kprintf("     Drag a title bar to move a window; the three dots\n");
    kprintf("     close, maximise and put away. Drag the bottom right\n");
    kprintf("     corner to resize, or a title bar to an edge to snap.\n");
    kprintf("     Alt and tab changes window, alt and an arrow snaps,\n");
    kprintf("     alt and d clears the desktop.\n");
    kprintf("     Pick a colour and drag on the white area to draw.\n");
    kprintf("     paint is not part of the kernel. It is a separate\n");
    kprintf("     program running in ring 3, drawing into pixels the\n");
    kprintf("     kernel mapped into it.\n");
    kprintf("     Escape returns you here.\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  2. Files that stay put\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     ls                       see what is stored\n");
    kprintf("     cat /doc/readme          read a file\n");
    kprintf("     write notes hello        make one\n");
    kprintf("     mkdir work               make a directory\n");
    kprintf("     cd work                  go into it, cd / comes back\n");
    kprintf("     disk                     the drive it is all kept on\n");
    kprintf("     You start in /home, which is yours. /doc is what\n");
    kprintf("     shipped, /cfg is what programs remember, /tmp is\n");
    kprintf("     emptied every boot.\n");
    kprintf("     Files are written to a real disk. Close this window,\n");
    kprintf("     start it again, and they will still be here.\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  3. The internet\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     dhcp                     ask the network for an address\n");
    kprintf("     net                      show what it got\n");
    kprintf("     ping 10.0.2.2            talk to the gateway\n");
    kprintf("     resolve example.com      look up a real name\n");
    kprintf("     fetch example.com / page.html\n");
    kprintf("     That last one downloads a live web page and saves it.\n");
    kprintf("     Then: cat page.html\n\n");

    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  4. Running real programs\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     exec /bin/hello          run a program and wait\n");
    kprintf("     bg /bin/count            run one in the background\n");
    kprintf("     exec /bin/wintest        prove a window crosses the ring\n");
    kprintf("     exec /bin/spawntest      a program starting a program\n");
    kprintf("     ps                       see what is running\n");
    kprintf("     These are separate executables, compiled on their\n");
    kprintf("     own. They run in ring 3, where they cannot touch\n");
    kprintf("     the kernel, and ask for everything through system\n");
    kprintf("     calls. ls /bin shows them; they live in the kernel\n");
    kprintf("     image rather than on the disk.\n\n");
    vga_set_color(VGA_LCYAN, VGA_BLACK);
    kprintf("  5. The machine itself\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
    kprintf("     cat /sys/memory          memory found and used\n");
    kprintf("     cat /sys/tasks           what is running\n");
    kprintf("     cat /sys/devices         what was found at boot\n");
    kprintf("     ls /sys                  the rest of it\n");
    kprintf("     Nothing in /sys is stored. Reading one of those\n");
    kprintf("     files runs the code that works out the answer.\n");
    kprintf("     fault                    divide by zero on purpose\n");
    kprintf("     The CPU catches that last one and the kernel prints\n");
    kprintf("     where it happened rather than just stopping.\n\n");

    vga_set_color(VGA_LGREEN, VGA_BLACK);
    kprintf("  None of this can harm your real computer. It runs inside\n");
    kprintf("  an emulator, on its own pretend machine and its own disk.\n\n");
    vga_set_color(VGA_LGREY, VGA_BLACK);
}
