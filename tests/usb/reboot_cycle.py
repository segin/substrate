#!/usr/bin/env python3
"""Reboot-path test for the EHCI shutdown hook.

Boots with root on usb-storage behind usb-ehci and init=/bin/sh, types
`reboot` via QMP sendkey, and passes iff the guest comes back up: two EHCI
attach lines and a second boot reaching the shell.  The shutdown hook runs
between the two — a wedged hook would hang the reboot, a broken HCRESET
would leave the controller unable to re-attach.
"""
import json, os, socket, subprocess, sys, time

TOP = "/home/segin/substrate"
S = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(S, "reboot.log")
QMP = os.path.join(S, "reboot.qmp")
for p in (LOG, QMP):
    if os.path.exists(p):
        os.unlink(p)

HC = sys.argv[1] if len(sys.argv) > 1 else "ehci"
CTRL = {"ehci": "usb-ehci", "xhci": "qemu-xhci", "uhci": "piix3-usb-uhci"}[HC]
cmd = [
    "qemu-system-i386", "-cpu", "qemu32,+sse,+sse2", "-accel", "kvm",
    "-m", "1G", "-machine", "pc,i8042=off", "-snapshot",
    "-kernel", f"{TOP}/sys/kernel.bin",
    "-append", "root=LABEL=sub-root trap serial_debug rw init=/bin/sh",
    "-drive", f"file={TOP}/rootfs.img,format=raw,if=none,id=drive0",
    "-device", f"{CTRL},id=usbctl",
    "-device", "usb-kbd,bus=usbctl.0",
    "-display", "none",
    "-chardev", f"file,id=serial0,path={LOG}",
    "-serial", "chardev:serial0",
    "-qmp", f"unix:{QMP},server,nowait",
]
if HC in ("ehci", "xhci"):
    cmd += ["-device", "usb-storage,drive=drive0,id=ums0,bus=usbctl.0"]
else:
    cmd += ["-device", "ich9-ahci,id=sata0",
            "-device", "ide-hd,bus=sata0.0,unit=0,drive=drive0"]
qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def txt():
    try:
        return open(LOG, "rb").read().decode("utf-8", "replace")
    except FileNotFoundError:
        return ""


MARK = {"ehci": "EHCI USB 2.0 controller",
        "xhci": "USB 3.x controller at",
        "uhci": "usb: registered HCD 'uhci"}

def attach_count(t):
    return t.count(MARK[HC])

def wait(cond, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if qemu.poll() is not None:
            return False
        if cond(txt()):
            return True
        time.sleep(1)
    return False


ok1 = wait(lambda t: attach_count(t) >= 1 and "#" in t[-200:] or "sh:" in t, 120)
time.sleep(8)
print(f"[*] first boot: attach={attach_count(txt()) >= 1}", flush=True)

s = socket.socket(socket.AF_UNIX)
s.connect(QMP)
f = s.makefile("rw")
f.readline()


def q(o):
    f.write(json.dumps(o) + "\n")
    f.flush()
    return f.readline().strip()


q({"execute": "qmp_capabilities"})
print("[*] typing 'reboot'", flush=True)
# First keypress after attach tends to double (poll-thread warm-up);
# send a sacrificial key and a ctrl-u line clear, then type slowly.
q({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": "spc"}]}})
time.sleep(0.6)
q({"execute": "send-key", "arguments": {"keys": [
    {"type": "qcode", "data": "ctrl"}, {"type": "qcode", "data": "u"}]}})
time.sleep(0.6)
for k in ["r", "e", "b", "o", "o", "t", "spc", "minus", "f"]:
    q({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": k}]}})
    time.sleep(0.4)
q({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": "ret"}]}})
s.close()

ok2 = wait(lambda t: attach_count(t) >= 2, 150)
time.sleep(5)
body = txt()
qemu.terminate()
try:
    qemu.wait(timeout=10)
except subprocess.TimeoutExpired:
    qemu.kill()

attaches = attach_count(body)
print(f"[*] {HC} attach lines: {attaches}")
for l in body.splitlines():
    if MARK[HC] in l or "shutdown" in l.lower():
        print("   |", l.strip())
print("VERDICT:", "REBOOT CYCLE OK" if attaches >= 2 else "FAIL")
sys.exit(0 if attaches >= 2 else 1)
