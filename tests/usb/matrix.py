#!/usr/bin/env python3
"""USB HCD regression matrix.

Configs (select by args; default: ehci-hid ehci-storage xhci-hid uhci-hid):
  ehci-hid     usb-ehci + usb-kbd/usb-mouse, AHCI root, QMP mouse injection
  ehci-storage root on usb-storage behind usb-ehci
  xhci-hid     qemu-xhci + usb-kbd/usb-mouse, AHCI root, QMP mouse injection
  uhci-hid     piix3-usb-uhci + usb-kbd/usb-mouse, AHCI root, QMP injection
  xhci-storage root on usb-storage behind qemu-xhci

Exit 0 iff every selected config passes.
"""
import json, os, socket, subprocess, sys, time

TOP = "/home/segin/substrate"
S = os.path.dirname(os.path.abspath(__file__))

CTRL = {
    "ehci": ["-device", "usb-ehci,id=usbctl"],
    "xhci": ["-device", "qemu-xhci,id=usbctl"],
    "uhci": ["-device", "piix3-usb-uhci,id=usbctl"],
}


def boot(tag, hc, root_usb, inject, timeout=210):
    log = os.path.join(S, f"m-{tag}.log")
    qmp = os.path.join(S, f"m-{tag}.qmp")
    for p in (log, qmp):
        if os.path.exists(p):
            os.unlink(p)
    cmd = [
        "qemu-system-i386", "-cpu", "qemu32,+sse,+sse2", "-accel", "kvm",
        "-m", "1G", "-machine", "pc,i8042=off", "-snapshot",
        "-kernel", f"{TOP}/sys/kernel.bin",
        "-append", "root=LABEL=sub-root trap mousedbg serial_debug rw",
        "-drive", f"file={TOP}/rootfs.img,format=raw,if=none,id=drive0",
        "-display", "none",
        "-chardev", f"file,id=serial0,path={log}",
        "-serial", "chardev:serial0",
        "-qmp", f"unix:{qmp},server,nowait",
    ] + CTRL[hc]
    if root_usb:
        cmd += ["-device", "usb-storage,drive=drive0,id=ums0,bus=usbctl.0"]
    else:
        cmd += ["-device", "ich9-ahci,id=sata0",
                "-device", "ide-hd,bus=sata0.0,unit=0,drive=drive0",
                "-device", "usb-kbd,bus=usbctl.0",
                "-device", "usb-mouse,bus=usbctl.0"]

    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                         stderr=subprocess.STDOUT)

    def txt():
        try:
            return open(log, "rb").read().decode("utf-8", "replace")
        except FileNotFoundError:
            return ""

    booted = None
    while time.time() - t0 < timeout:
        if p.poll() is not None:
            break
        t = txt()
        if "Starting init" in t or "login:" in t:
            booted = time.time() - t0
            break
        time.sleep(1)

    reports = 0
    if inject and booted:
        time.sleep(8)
        try:
            s = socket.socket(socket.AF_UNIX)
            s.connect(qmp)
            f = s.makefile("rw")
            f.readline()

            def q(o):
                f.write(json.dumps(o) + "\n")
                f.flush()
                return f.readline().strip()

            q({"execute": "qmp_capabilities"})
            for i in range(30):
                q({"execute": "input-send-event", "arguments": {"events": [
                    {"type": "rel", "data": {"axis": "x", "value": 5}},
                    {"type": "rel", "data": {"axis": "y", "value": 3}}]}})
                time.sleep(0.05)
            for _ in range(2):
                q({"execute": "input-send-event", "arguments": {"events": [
                    {"type": "btn", "data": {"button": "left", "down": True}}]}})
                time.sleep(0.1)
                q({"execute": "input-send-event", "arguments": {"events": [
                    {"type": "btn", "data": {"button": "left", "down": False}}]}})
                time.sleep(0.1)
            s.close()
        except OSError as e:
            print(f"    qmp error: {e}")
        time.sleep(5)
        reports = txt().count("mouse: raw[")

    body = txt()
    p.terminate()
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()

    errs = [l for l in body.splitlines()
            if any(k in l.lower() for k in ("panic", "oops"))
            or (hc in l.lower() and
                any(k in l.lower() for k in ("fail", "timeout", "error")))]
    ok = bool(booted) and not errs and (not inject or reports > 0)
    print(f"  {tag:14s} boot={'%.0fs' % booted if booted else 'FAIL':5s} "
          f"reports={reports if inject else '-':<4} errs={len(errs)} "
          f"-> {'PASS' if ok else 'FAIL'}")
    for l in errs[:5]:
        print("    |", l.strip())
    return ok


ALL = {
    "ehci-hid":     lambda: boot("ehci-hid", "ehci", False, True),
    "ehci-storage": lambda: boot("ehci-storage", "ehci", True, False),
    "xhci-hid":     lambda: boot("xhci-hid", "xhci", False, True),
    "uhci-hid":     lambda: boot("uhci-hid", "uhci", False, True),
    "xhci-storage": lambda: boot("xhci-storage", "xhci", True, False),
}

sel = sys.argv[1:] or ["ehci-hid", "ehci-storage", "xhci-hid", "uhci-hid"]
res = [ALL[k]() for k in sel]
print(f"MATRIX: {'PASS' if all(res) else 'FAIL'} ({sum(res)}/{len(res)})")
sys.exit(0 if all(res) else 1)
