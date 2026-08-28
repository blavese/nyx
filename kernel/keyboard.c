/* PS/2 keyboard, scancode set 1, with a small ring buffer so keystrokes
   taken during an interrupt survive until someone reads them. */
#include "keyboard.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "serial.h"

#define BUFSZ 256
static volatile char buf[BUFSZ];
static volatile u32 head = 0, tail = 0;

static bool shift = false, caps = false, ctrl = false;

static const char MAP[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  0x5C,'z','x','c','v','b','n','m',',','.','/',
    0,  '*', 0, ' ',
};

static const char MAP_SHIFT[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',
    0,  '*', 0, ' ',
};

static void push(char c) {
    u32 next = (head + 1) % BUFSZ;
    if (next != tail) { buf[head] = c; head = next; }
}

static void on_key(registers_t *r) {
    u8 sc = inb(0x60);

    if (sc & 0x80) {                       /* key release */
        u8 code = sc & 0x7F;
        if (code == 0x2A || code == 0x36) shift = false;
        if (code == 0x1D) ctrl = false;
        return;
    }

    switch (sc) {
        case 0x2A: case 0x36: shift = true; return;
        case 0x1D: ctrl = true; return;
        case 0x3A: caps = !caps; return;
        default: break;
    }

    if (sc >= 128) return;
    char c = shift ? MAP_SHIFT[sc] : MAP[sc];
    if (!c) return;

    if (caps && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    else if (caps && shift && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');

    if (ctrl && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);   /* ^A..^Z */
    push(c);
}

void keyboard_init(void) {
    register_interrupt_handler(33, on_key);
    pic_unmask(1);
}

bool kbd_has_char(void) { return head != tail; }

int kbd_trygetchar(void) {
    if (head == tail) {
        /* Also accept input from the serial line, which is how the
           automated tests drive the shell. It arrives on IRQ4 and is
           buffered there, so nothing is lost between polls. */
        int s = serial_trygetc();
        if (s >= 0) return s == 13 ? 10 : s;   /* CR becomes LF */
        return -1;
    }
    char c = buf[tail];
    tail = (tail + 1) % BUFSZ;
    return (u8)c;
}

char kbd_getchar(void) {
    for (;;) {
        int c = kbd_trygetchar();
        if (c >= 0) return (char)c;
        __asm__ volatile ("hlt");
    }
}
