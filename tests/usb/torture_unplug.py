#!/usr/bin/env python3
"""device_del mid-I/O torture: yank a usb-storage device while dd reads it.

Root stays on AHCI; a second usb-storage disk hangs off the controller under
test.  Pass criteria: the disconnect propagates (usb 'device removed' or msc
error), the machine stays alive (a typed `echo ALIVE` lands after the yank),
and nothing panics.  Exercises the RF-1a/RF-4 error classification and the
disconnect paths on all three HCDs.

usage: torturetest.py {ehci|xhci|uhci}
"""
import json, os, socket, subprocess, sys, time

TOP = "/home/segin/substrate"
S = os.path.dirname(os.path.abspath(__file__))
HC = sys.argv[1] if len(sys.argv) > 1 else "ehci"
CTRL = {"ehci": "usb-ehci", "xhci": "qemu-xhci", "uhci": "piix3-usb-uhci"}[HC]
LOG = os.path.join(S, f"torture-{HC}.log")
QMP = os.path.join(S, f"torture-{HC}.qmp")
SCRATCH = os.path.join(S, f"torture-{HC}.img")

for p in (LOG, QMP):
    if os.path.exists(p):
        os.unlink(p)
if not os.path.exists(SCRATCH):
    with open(SCRATCH, "wb") as f:
        f.truncate(64 * 1024 * 1024)

cmd = [
    "qemu-system-i386", "-cpu", "qemu32,+sse,+sse2", "-accel", "kvm",
    "-m", "1G", "-machine", "pc,i8042=off", "-snapshot",
    "-kernel", f"{TOP}/sys/kernel.bin",
    "-append", "root=LABEL=sub-root trap serial_debug rw init=/bin/sh",
    "-drive", f"file={TOP}/rootfs.img,format=raw,if=none,id=drive0",
    "-device", "ich9-ahci,id=sata0",
    "-device", "ide-hd,bus=sata0.0,unit=0,drive=drive0",
    "-drive", f"file={SCRATCH},format=raw,if=none,id=scratch",
    "-device", f"{CTRL},id=usbctl",
    "-device", "usb-kbd,bus=usbctl.0",
    "-device", "usb-storage,drive=scratch,id=victim,bus=usbctl.0",
    "-display", "none",
    "-chardev", f"file,id=serial0,path={LOG}",
    "-serial", "chardev:serial0",
    "-qmp", f"unix:{QMP},server,nowait",
]
qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)


def txt():
    try:
        return open(LOG, "rb").read().decode("utf-8", "replace")
    except FileNotFoundError:
        return ""


def wait(cond, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if qemu.poll() is not None:
            return False
        if cond(txt()):
            return True
        time.sleep(1)
    return False


assert wait(lambda t: "Entering main loop" in t or "Trying /bin/sh" in t, 150), "no boot"
time.sleep(10)

s = socket.socket(socket.AF_UNIX)
s.connect(QMP)
f = s.makefile("rw")
f.readline()


def q(o):
    f.write(json.dumps(o) + "\n")
    f.flush()
    return f.readline().strip()


q({"execute": "qmp_capabilities"})

QCODE = {'a':'a','b':'b','c':'c','d':'d','e':'e','f':'f','g':'g','h':'h',
         'i':'i','j':'j','k':'k','l':'l','m':'m','n':'n','o':'o','p':'p',
         'q':'q','r':'r','s':'s','t':'t','u':'u','v':'v','w':'w','x':'x',
         'y':'y','z':'z','0':'0','1':'1','2':'2','3':'3','4':'4','5':'5',
         '6':'6','7':'7','8':'8','9':'9',' ':'spc','/':'slash','=':'equal',
         '.':'dot','-':'minus'}
SHIFT = {'&': '7', 'A':'a','L':'l','I':'i','V':'v','E':'e'}


def type_line(line):
    # settle the first-key doubling, then clear the line
    q({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": "spc"}]}})
    time.sleep(0.5)
    q({"execute": "send-key", "arguments": {"keys": [
        {"type": "qcode", "data": "ctrl"}, {"type": "qcode", "data": "u"}]}})
    time.sleep(0.4)
    for ch in line:
        if ch in SHIFT:
            q({"execute": "send-key", "arguments": {"keys": [
                {"type": "qcode", "data": "shift"},
                {"type": "qcode", "data": SHIFT[ch]}]}})
        else:
            q({"execute": "send-key", "arguments": {"keys": [
                {"type": "qcode", "data": QCODE[ch]}]}})
        time.sleep(0.25)
    q({"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": "ret"}]}})


# The scratch disk is the only SCSI device (root is AHCI): scsi0.
count = "8" if HC == "uhci" else "48"          # FS storage is ~1 MB/s
print(f"[*] starting dd on {HC}", flush=True)
type_line(f"dd if=/dev/storage/scsi0 of=/dev/null bs=1m count={count} &")
time.sleep(2 if HC != "uhci" else 3)           # let dd get mid-transfer

print("[*] yanking the device", flush=True)
print("   ", q({"execute": "device_del", "arguments": {"id": "victim"}}), flush=True)

# The dying device's retries hold submit_lock for full bulk timeouts, and
# the keyboard shares that lock -- typing mid-storm starves.  Wait for the
# detach to complete, then check liveness.
ok_detach = wait(lambda t: "detached device" in t, 150)
print(f"[*] detach completed: {ok_detach}", flush=True)
time.sleep(5)

print("[*] liveness check", flush=True)
type_line("echo ALIVE")
ok_alive = wait(lambda t: "ALIVE" in t, 60)

body = txt()
qemu.terminate()
try:
    qemu.wait(timeout=10)
except subprocess.TimeoutExpired:
    qemu.kill()

removed = ("device removed" in body) or ("disconnect" in body.lower())
panic = ("panic" in body.lower()) or ("oops" in body.lower())
for l in body.splitlines():
    if any(k in l.lower() for k in ("removed", "disconnect", "msc", "panic",
                                    "reset recovery", "i/o error")):
        print("   |", l.strip()[:120])
print(f"[*] alive={ok_alive} removal_seen={removed} panic={panic}")
verdict = ok_alive and removed and not panic
print(f"VERDICT[{HC}]:", "TORTURE OK" if verdict else "FAIL")
sys.exit(0 if verdict else 1)
