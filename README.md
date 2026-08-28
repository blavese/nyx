# nyx

A small operating system written from scratch for 32-bit x86. It boots from a
multiboot loader, draws to a framebuffer, manages its own memory, preempts its
own tasks, keeps files on a FAT16 disk, talks to the internet, and runs real
programs in ring 3.

![nyx booting](docs/boot.png)

It is not a clone of anything. About 5,000 lines of C and assembly, no libc,
no third-party code, no runtime dependencies.

## running it on Windows

Download **nyx.exe** from the
[latest release](https://github.com/blavese/nyx/releases/latest) and run it.
The kernel is inside that file; nothing needs to be built.

The launcher checks for QEMU, the emulator nyx boots on, and offers to install
it from Microsoft's package manager if it is missing. Then press Start and a
black window opens with the operating system running in it. Type `guide` when
you get there.

It runs as a normal user, needs no administrator rights, and cannot affect
Windows: the kernel only ever sees the pretend machine QEMU gives it. Your
files live in a disk image under `%LocalAppData%\nyx`.

Something to try once it boots:

    exec hello.elf
    bg count.elf
    ps

Those are separate executables, compiled on their own and loaded from the disk.
They run in ring 3 and reach the kernel only through system calls.

    dhcp
    fetch example.com / page.html
    cat page.html

That gets an address from the network, downloads a live web page over TCP, and
saves it to a disk that survives closing the window.

## running it from source

You need QEMU and Zig. Zig is used only as a cross compiler, so there is no
i686-elf toolchain to build first.

    ./run.sh          boot in a window
    ./run.sh -t       boot headless, console on stdout
    ./run.sh -T       run the built-in self test, exit code is the result

`run.sh` creates a disk image and attaches a network card automatically.

The Windows launcher lives in `launcher/` and is built with
`dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true`.
It embeds `build/nyx.elf` and a starter disk, so build the kernel and run
`userland/build.sh` first.

## what it actually does

**Boot.** A multiboot header gets it loaded at 1 MiB in 32-bit protected mode.
The bootloader's memory map is read to find out how much RAM exists.

**Descriptor tables.** A flat GDT (kernel and user code/data) plus a TSS, which
is how an interrupt taken in ring 3 finds a kernel stack to switch to. A 256-entry IDT with
stubs for all 32 CPU exceptions and 16 hardware IRQs, generated rather than
hand-written.

**Interrupts.** The 8259 PICs are remapped off the exception vectors to 32..47.
Exceptions that nothing handles print a register dump and halt, instead of
silently triple faulting.

**Physical memory.** A bitmap allocator, one bit per 4 KiB frame, built from
the firmware memory map. The kernel image and the bitmap itself are marked in
use so they can never be handed out.

**Virtual memory.** Two-level paging. The low 16 MiB is identity mapped so that
enabling paging does not move the ground out from under the kernel, and
map_page / unmap_page / virt_to_phys work for anything above that. Page faults
report the faulting address and whether it was a read or a write.

**Heap.** First-fit free list with boundary tags, coalescing neighbours on
free. kmalloc, kcalloc, kfree.

**Tasks.** Round-robin preemptive multitasking. Each task owns a kernel stack
holding a complete interrupt frame, so switching is a matter of telling the
interrupt return path to unwind a different one. Tasks sleep, yield and exit,
and dead ones are reaped.

**Disk.** An ATA PIO driver for the primary bus: IDENTIFY to find the drive and
its size, then LBA28 reads and writes with a cache flush. PIO moves every word
through the CPU, which is slow and needs no bus mastering setup, which is the
right trade at this size.

**Filesystem.** FAT16, so the disk is not a sealed box: other tools can open
the image and files move in both directions. Files are worked on in memory and
written through on every change. A blank disk is formatted automatically on
first boot. `tools/readfat.py` parses the image straight from the
specification, sharing no code with the kernel, and can copy a file in from the
host.

**Network.** PCI enumeration to find the card, then a Realtek RTL8139 driver
with an interrupt-driven receive ring and four transmit descriptors. On top of
that: ethernet, ARP with a cache, IPv4 with checksums, ICMP (it answers pings
and sends them), UDP, a DHCP client, a DNS resolver, and a single-connection
TCP client with a proper three way handshake and orderly close. `fetch` uses
all of it to do an HTTP GET.

**Graphics.** Mode setting through the Bochs VBE dispatch ports rather than a
BIOS call, so it works from protected mode with no real mode trampoline and no
help from the bootloader. The aperture is found through the VGA device's PCI
BAR and mapped explicitly. Drawing goes to a back buffer and is pushed to the
card in one go, because compositing directly in video memory over PCI is
visibly slow. The console is redrawn on top of that with a bitmap font, so
everything that already printed kept working.

**Programs.** Ring 3, its own address space per process, and seven system calls
through int 0x80. An ELF32 loader maps each PT_LOAD segment where the file asks
and refuses anything that would land in kernel memory. `userland/` holds
programs built entirely separately: the only thing they share with the kernel
is the syscall numbers.

**Drivers.** Framebuffer and VGA text consoles, PS/2 keyboard with
shift/caps/ctrl, PS/2 mouse with a drawn pointer, PIT at 100 Hz, and a 16550
serial port driven by IRQ4.

**Shell.** Reads from the keyboard or the serial line, whichever produces a
character first, so a person can type at it and a script can pipe into it.

## testing

The kernel tests itself. `./run.sh -T` boots with selftest on the command line,
runs 71 checks across every subsystem, then writes to QEMU's debug-exit port so
the host gets a real exit status.

    [string]           8 checks      [disk]         6 checks
    [physical memory]  4 checks      [fat]          9 checks
    [paging]           4 checks      [network]      7 checks
    [heap]             5 checks      [elf]          7 checks
    [filesystem]       6 checks      [userspace]    3 checks
    [timer]            2 checks      [video]        7 checks
    [interrupts]       2 checks      [mouse]        1 check

    71 passed, 0 failed
    SELFTEST_PASS

The tests are written to fail for the right reasons. The disk test writes a
pattern to a spare sector, reads it back, and restores the original. The FAT
test writes a file spanning several clusters, so it exercises chain following
rather than a single sector. The network test performs a real DHCP handshake,
pings the gateway and resolves a live hostname. The ELF test feeds the loader
six malformed images, including one asking to be mapped over the kernel, and
requires each to be refused. The userspace test watches the system call counter
rather than the task list, because a program can finish before a count is
taken.

`tools/shell_test.sh` is the other half. It boots the OS, types commands at the
shell over the serial line, and checks what comes back.

## things that went wrong

The interesting part of writing a kernel is that nothing catches you. Every one
of these presented as a machine that stopped, with no message.

**memset called itself.** At -O2 clang recognises the byte-fill loop inside
memset as a memset, and replaces the body with a call to memset. It recursed
until the stack was gone. -fno-builtin does not stop the loop idiom pass; the
fix was volatile pointers in the three byte movers.

**The compiler emitted SSE.** Zig's default x86 target has SSE2 on, so clang
used movd xmm0 for a 64-bit integer move. The CPU had never been told the FPU
exists, so the first one raised an invalid opcode fault, far from anything that
looked related. Fixed with -mcpu=i686, and tools/check_sse.py now fails the
build if an SSE opcode reaches an executable section.

**The TSS descriptor got an address where it wanted a length.** Passing
base + size - 1 instead of size - 1 as the limit made ltr fault, which triple
faulted the machine one instruction into gdt_init.

**One timer tick, then silence.** The PIC will not deliver another interrupt at
the same or lower priority until it is acknowledged. The end-of-interrupt write
was missing, so exactly one IRQ0 arrived and the kernel waited forever for the
second.

**Serial input arrived shredded.** Piping a command into the shell produced
"notes.txshell" instead of "notes.txt shell". Instrumenting both ends settled
it: for a 26 byte burst the kernel reported irqs=3 got=4 read=4 dropped=0. The
receive path had lost nothing, because it was never given the bytes. QEMU's
-serial stdio backend does not apply back pressure to a pipe. The kernel now
takes IRQ4 and buffers into a ring, and the harness types at human speed, which
the guest keeps up with exactly (irqs=104 got=104 read=104 dropped=0).

## what it does not do

Being explicit about the boundary, because "operating system" covers a very
large range:

- **No fork or exec in the Unix sense.** A program is loaded and run; it
  cannot start another or replace itself.
- **Seven system calls.** Enough to print, read a file, sleep and exit.
- **Flat filesystem**, no directories, and only 8.3 names.
- **No shared libraries**, no dynamic linking, no relocation: programs are
  static and loaded at a fixed address.
- **No window system.** There is a framebuffer, a font and a mouse pointer,
  but nothing draws windows yet.
- **TCP has no retransmission** and handles one connection at a time. It is
  correct over a link that does not drop packets, which is what an emulated
  network is, and would need real work before it faced the open internet
  directly.
- **No TLS**, so `fetch` is plain HTTP only.
- **Ping only reaches the local network.** ICMP is implemented in both
  directions and pinging the gateway works. QEMU's user mode networking does
  not forward ICMP to the wider internet without elevated privileges, so
  pinging an outside address times out even though DNS and TCP to that same
  address work.
- **32-bit only**, single processor, no SMP and no APIC.

It is a real kernel in that it boots on the bare machine, drives its own
hardware, and can fetch a file from a real server and keep it on a real disk.
It is not something you would run anything important on, and it is several
orders of magnitude away from Linux, which is roughly 30 million lines.

## layout

    boot/boot.S        multiboot header and entry point
    kernel/gdt.c       segments and the task state segment
    kernel/idt.c       interrupt descriptor table and dispatch
    kernel/isr.S       the 49 interrupt stubs (generated)
    kernel/pic.c       8259 remapping
    kernel/pmm.c       physical frame allocator
    kernel/paging.c    two-level paging
    kernel/heap.c      kmalloc
    kernel/sched.c     preemptive round-robin tasks
    kernel/ata.c       ata pio disk driver
    kernel/diskfs.c    reading and writing the filesystem image
    kernel/fs.c        the filesystem itself
    kernel/pci.c       pci configuration space
    kernel/rtl8139.c   network card driver
    kernel/net.c       ethernet, arp, ip, icmp, udp, dhcp, dns
    kernel/tcp.c       tcp client
    kernel/http.c      http get
    kernel/fb.c        linear framebuffer via the bochs vbe ports
    kernel/fbcon.c     the text console drawn into it
    kernel/font.c      8x16 bitmap font (generated)
    kernel/mouse.c     ps/2 mouse and the drawn pointer
    kernel/fat.c       fat16
    kernel/elf.c       elf32 loader
    kernel/syscall.c   the system call table
    kernel/user.c      building and launching ring 3 processes
    kernel/vga.c       text console
    kernel/serial.c    16550 uart, interrupt driven
    kernel/keyboard.c  ps/2 keyboard
    kernel/timer.c     programmable interval timer
    kernel/shell.c     the shell
    kernel/welcome.c   the first-run text and the guided tour
    kernel/selftest.c  the boot-time test suite
    kernel/divide.c    64-bit division helpers libgcc would normally provide
    userland/          programs, built separately from the kernel
    tools/             build checks, the font generator, the FAT reader
    launcher/          the Windows launcher (C#/WPF)

## license

MIT, see LICENSE.
