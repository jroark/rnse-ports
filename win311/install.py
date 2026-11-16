#!/usr/bin/env python3
"""A disk image with MS-DOS 6.22 and Windows 3.11 for the PC emulator,
installed from your own floppy images in QEMU (qemu-system-i386):

    win311/install.py DOS_DIR WIN_DIR [OUT.IMG]

DOS_DIR holds the three setup disks of MS-DOS 6.22 (Disk1.img ..
Disk3.img), WIN_DIR the six of Windows 3.11 (DISK1.IMG .. DISK6.IMG),
1.44 MB images.  Neither is in the repository, nor is what this makes:
both are Microsoft's.  OUT.IMG (default win311/work/img/WIN311.IMG) is
a 64 MiB disk of 130 cylinders, 16 heads, 63 sectors, as Tiny386 and
QEMU both see it.

Setup is typed through QEMU's monitor (sendkey, change floppy0), each
step when the screen has stood still for a while (a blinking cursor
aside), so that a slow or a fast machine both work; QEMU's window shows
it happen, and a screendump of each step goes to OUT.IMG's folder,
install-shots/.  DOS from its floppies; Windows from a copy of its six
disks on the hard disk (C:\WININST, removed afterwards), since its Setup
asks for them in an order of its own.  Express Setup, no printer, no
tutorial, the name "RNS-E".  Then the image is set for the 192 with mtools
(win311/docker/settings.sh): straight to Windows, no pauses, no swap
file (docs/win311.md).
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def find_disk(folder, n):
    for name in os.listdir(folder):
        if name.upper() == "DISK%d.IMG" % n:
            return os.path.join(folder, name)
    sys.exit("%s: no disk %d" % (folder, n))


class Monitor:
    def __init__(self, path):
        for _ in range(50):
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.2)
        else:
            sys.exit("QEMU's monitor did not answer")
        self.s.settimeout(0.3)
        self.drain()

    def drain(self):
        try:
            while self.s.recv(65536):
                pass
        except socket.timeout:
            pass

    def cmd(self, c):
        self.s.sendall(c.encode() + b"\n")
        time.sleep(0.15)
        self.drain()

    def keys(self, *keys):
        for k in keys:
            self.cmd("sendkey " + k)

    def type(self, text):
        for ch in text:
            if ch == ":":
                self.keys("shift-semicolon")
            elif ch == "-":
                self.keys("minus")
            elif ch == "\\":
                self.keys("backslash")
            elif ch.isupper():
                self.keys("shift-" + ch.lower())
            else:
                self.keys(ch)


def pixels(path):
    data = open(path, "rb").read()
    parts = data.split(maxsplit=4)
    return parts[4]


def wait_still(mon, shots, name, still=7, every=3, timeout=900):
    """Until seven screendumps in a row, 18 s, differ in fewer than 300
    pixels.  (Windows' Setup can sit at one per cent for many seconds
    while it unpacks a big file, with Exit Setup the default button.)"""
    path = os.path.join(shots, name + ".ppm")
    last, same, start = None, 0, time.time()
    while time.time() - start < timeout:
        mon.cmd("screendump " + path)
        time.sleep(0.4)
        try:
            now = pixels(path)
        except (OSError, IndexError):
            time.sleep(every)
            continue
        if last is not None and len(now) == len(last):
            diff = sum(1 for i in range(0, len(now), 3) if now[i:i + 3] != last[i:i + 3])
            same = same + 1 if diff < 300 else 0
            if same >= still - 1:
                print("  " + name)
                return
        last = now
        time.sleep(every)
    sys.exit("the screen did not settle at step '%s' (see %s)" % (name, path))


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    dos, win = sys.argv[1], sys.argv[2]
    img = os.path.abspath(sys.argv[3] if len(sys.argv) == 4 else os.path.join(HERE, "work/img/WIN311.IMG"))
    if not shutil.which("qemu-system-i386"):
        sys.exit("qemu-system-i386 is needed")
    work = tempfile.mkdtemp()
    shots = os.path.join(os.path.dirname(img), "install-shots")
    os.makedirs(shots, exist_ok=True)
    floppy = {}
    for n in (1, 2, 3):
        floppy["D%d" % n] = shutil.copy(find_disk(dos, n), os.path.join(work, "D%d.IMG" % n))
    for n in range(1, 7):
        floppy["W%d" % n] = shutil.copy(find_disk(win, n), os.path.join(work, "W%d.IMG" % n))
    with open(img, "wb") as f:
        f.truncate(130 * 16 * 63 * 512)

    def qemu(boot, steps):
        sock = os.path.join(work, "monitor")
        if os.path.exists(sock):
            os.unlink(sock)
        args = ["qemu-system-i386", "-M", "pc", "-cpu", "486", "-m", "16", "-vga", "std",
                "-drive", "file=%s,format=raw,if=ide,index=0" % img,
                "-rtc", "base=localtime", "-monitor", "unix:%s,server,nowait" % sock]
        if boot == "a":
            args += ["-drive", "file=%s,format=raw,if=floppy,index=0" % floppy["D1"], "-boot", "a"]
        else:
            args += ["-boot", "c"]
        q = subprocess.Popen(args)
        try:
            m = Monitor(sock)
            for name, act in steps:
                wait_still(m, shots, name)
                act(m)
            m.cmd("quit")
            q.wait(timeout=30)
        finally:
            if q.poll() is None:
                q.kill()

    def change(m, d):
        m.cmd("change floppy0 %s raw" % floppy[d])

    def mtools(script):
        """mtools in the container: c: the image, a: .. g: Windows' disks.
        The image goes in and comes out through a pipe, not a shared
        folder: Docker's sharing on a Mac can show a container a file as
        it was before QEMU last wrote it."""
        container(script, ["-v", "%s:/f:ro" % work])

    def container(script, extra=()):
        rc = 'drive c: file="/tmp/c.img" partition=1\n'
        for n, letter in enumerate("abdefg", 1):
            rc += 'drive %s: file="/f/W%d.IMG"\n' % (letter, n)
        with open(img, "rb") as f:
            data = f.read()
        out = subprocess.run(["docker", "run", "--rm", "-i", "-e", "MTOOLS_SKIP_CHECK=1", "-e", "RC=" + rc] +
                             list(extra) + ["rnse-seabios", "sh", "-c",
                              "set -e; cat > /tmp/c.img; printf '%s' \"$RC\" > /root/.mtoolsrc; "
                              "(" + script + ") >&2; cat /tmp/c.img"],
                             input=data, stdout=subprocess.PIPE, check=True).stdout
        if len(out) != len(data):
            sys.exit("the image came back from the container %d bytes long" % len(out))
        with open(img, "wb") as f:
            f.write(out)

    if subprocess.run(["docker", "image", "inspect", "rnse-seabios"], capture_output=True).returncode:
        subprocess.run(["docker", "build", "-q", "-t", "rnse-seabios", os.path.join(HERE, "docker")], check=True)

    print("MS-DOS 6.22 Setup")
    qemu("a", [
        ("00-dos-welcome", lambda m: m.keys("ret")),
        ("01-dos-unallocated", lambda m: m.keys("ret")),
        ("02-dos-restart", lambda m: m.keys("ret")),
        ("03-dos-settings", lambda m: m.keys("ret")),
        ("04-dos-directory", lambda m: m.keys("ret")),
        ("05-dos-disk2", lambda m: (change(m, "D2"), m.keys("ret"))),
        ("06-dos-disk3", lambda m: (change(m, "D3"), m.keys("ret"))),
        ("07-dos-remove", lambda m: (m.cmd("eject floppy0"), m.keys("ret"))),
        ("08-dos-complete", lambda m: m.keys("ret")),
        ("09-dos-prompt", lambda m: None),
    ])

    # Windows' six disks into C:\WININST, so that its Setup finds all it
    # wants there and asks for no disk (it asks for them in an order that
    # depends on how fast it runs).
    print("Windows 3.11's disks to C:\\WININST")
    mtools("mmd c:/WININST; for d in a b d e f g; do mcopy -n -s \"$d:*\" c:/WININST/; done")

    print("Windows 3.11 Setup")
    qemu("c", [
        ("10-dos-prompt", lambda m: (m.type("c:\\wininst\\setup"), m.keys("ret"))),
        ("11-win-welcome", lambda m: m.keys("ret")),
        ("12-win-express", lambda m: m.keys("ret")),
        ("13-win-name", lambda m: (m.type("RNS-E"), time.sleep(1), m.keys("ret"))),
        ("14-win-verify", lambda m: m.keys("ret")),
        ("15-win-printer", lambda m: m.keys("ret")),
        ("16-win-applications", lambda m: m.keys("ret")),
        ("17-win-tutorial", lambda m: m.keys("alt-s")),
        ("18-win-done", lambda m: m.keys("alt-d")),
        ("19-dos-again", lambda m: None),
    ])
    mtools("mdeltree c:/WININST")
    shutil.rmtree(work, ignore_errors=True)

    print("Settings for the 192")
    container("sh /s/settings.sh", ["-v", "%s:/s:ro" % os.path.join(HERE, "docker")])
    print(img)


if __name__ == "__main__":
    main()
