"""Boots nyx headless, opens the desktop, draws in paint, and looks at the screen.

The self test can prove the window server hands out a surface, but not that
anything reaches the display or that input comes back. This drives QEMU's
monitor to move the real mouse and to grab the real screen.

paint is a ring 3 program, so a stroke appearing on screen means the whole
path worked: window manager, event queue, int 0x80, a program outside the
kernel writing its own pixels, and the compositor picking them up.

  python tools/shotcheck.py [--keep]
"""
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOT = os.path.join(ROOT, "build", "desktop.ppm")
MONITOR_PORT = 55731

QEMU = os.environ.get("QEMU") or "C:/Program Files/qemu/qemu-system-i386.exe"
if not os.path.exists(QEMU):
    from shutil import which
    QEMU = which("qemu-system-i386") or QEMU

# paint opens at 40,40 with a 640x420 content area, a 24 pixel title bar and a
# 58 pixel toolbar, so its canvas is this rectangle of the screen.
CANVAS = (60, 140, 660, 470)          # left, top, right, bottom

PAPER = (0xF2, 0xF5, 0xF7)
RED = (0xC7, 0x4A, 0x3C)

# Where to press, then the drag from there. Screen coordinates.
STROKE_START = (300, 300)
STROKE = [(120, 40), (40, 90), (-90, 30)]


class Monitor:
    """QEMU's text monitor over TCP. Used to move the mouse and grab the
    screen, neither of which the guest can be asked to do for us."""

    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=10)
        time.sleep(0.4)
        self.s.recv(65536)

    def send(self, command, settle=0.3):
        self.s.sendall((command + "\n").encode())
        time.sleep(settle)
        try:
            return self.s.recv(65536).decode("utf-8", "replace")
        except socket.timeout:
            return ""

    def close(self):
        self.s.close()


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise ValueError("not a P6 ppm")
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1
    w, h, _maxval = fields
    return w, h, data[pos:pos + w * h * 3]


def count_in(px, w, rect, rgb):
    left, top, right, bottom = rect
    want = bytes(rgb)
    n = 0
    for y in range(top, bottom):
        row = y * w * 3
        for x in range(left, right):
            i = row + x * 3
            if px[i:i + 3] == want:
                n += 1
    return n


def main():
    kernel = os.path.join(ROOT, "build", "nyx.elf")
    subprocess.run(["bash", "build.sh"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)

    if os.path.exists(SHOT):
        os.remove(SHOT)

    cmd = [
        QEMU, "-kernel", kernel, "-m", "64", "-no-reboot", "-display", "none",
        "-serial", "stdio",
        "-monitor", "tcp:127.0.0.1:%d,server,nowait" % MONITOR_PORT,
    ]
    proc = subprocess.Popen(cmd, cwd=ROOT, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    try:
        time.sleep(4.5)                        # let it boot
        for ch in "desktop\n":                 # typed, not pasted: see shell_test.sh
            proc.stdin.write(ch.encode())
            proc.stdin.flush()
            time.sleep(0.05)
        time.sleep(4.0)                        # paint starts and draws

        mon = Monitor(MONITOR_PORT)

        # The pointer is relative, so walk it into the corner for a known
        # starting point. One large jump gets clamped by the PS/2 model.
        for _ in range(12):
            mon.send("mouse_move -200 -200", settle=0.08)

        mon.send("mouse_move %d %d" % STROKE_START, settle=0.5)
        mon.send("mouse_button 1", settle=0.4)
        for dx, dy in STROKE:
            mon.send("mouse_move %d %d" % (dx, dy), settle=0.35)
        mon.send("mouse_button 0", settle=0.5)
        time.sleep(1.5)

        out = mon.send("screendump %s" % SHOT.replace("\\", "/"), settle=2.5)
        mon.close()
        if not os.path.exists(SHOT):
            print("no screenshot was produced")
            print(out)
            return 1
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        proc.kill()
        proc.wait(timeout=10)

    w, h, px = read_ppm(SHOT)
    print("screen %dx%d" % (w, h))

    counts = {}
    for i in range(0, len(px), 3):
        counts[px[i:i + 3]] = counts.get(px[i:i + 3], 0) + 1

    def seen(rgb, at_least=1):
        return counts.get(bytes(rgb), 0) >= at_least

    ink = count_in(px, w, CANVAS, RED)
    paper = count_in(px, w, CANVAS, PAPER)

    checks = [
        # paint fills its canvas with paper white before anything else
        ("paint's canvas is on screen",     seen(PAPER, 20000)),
        # palette swatches, drawn by the ring 3 program
        ("its red swatch is on screen",     seen(RED, 100)),
        ("its green swatch is on screen",   seen((0x2E, 0x9E, 0x5B), 300)),
        ("its purple swatch is on screen",  seen((0x8E, 0x6B, 0xE0), 300)),
        # the toolbar the program drew, and the chrome the kernel drew round it
        ("its toolbar is on screen",        seen((0x22, 0x29, 0x31), 5000)),
        ("the window has a title bar",      seen((0x2C, 0x7A, 0x6B), 500)),
        ("the desktop is behind it",        seen((0x1B, 0x22, 0x2A), 50000)),
        # and the part that needs input to have made it all the way across
        ("the canvas started out blank",    paper > 100000),
        ("a mouse stroke painted on it",    ink > 400),
    ]

    failed = 0
    for name, good in checks:
        print("  %s  %s" % ("PASS" if good else "FAIL", name))
        if not good:
            failed += 1

    print("canvas: %d painted pixels, %d still blank" % (ink, paper))

    if "--keep" not in sys.argv:
        os.remove(SHOT)
    else:
        print("screenshot kept at %s" % SHOT)

    print("\ndesktop check: %s" %
          ("all checks passed" if not failed else "%d failed" % failed))
    return failed


if __name__ == "__main__":
    sys.exit(main())
