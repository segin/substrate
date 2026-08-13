# USB HCD regression harnesses

QEMU-host scripts used by the ehci-audit and RF-series commits.  All boot
`sys/kernel.bin` against `rootfs.img` with `-snapshot` (no image writes).

- `matrix.py [configs...]` — boot matrix: ehci-hid / ehci-storage /
  xhci-hid / uhci-hid / xhci-storage.  HID configs inject mouse motion +
  clicks over QMP and count decoded `mousedbg` reports.
- `reboot_cycle.py {ehci|xhci|uhci}` — boots (root on usb-storage for
  ehci/xhci), types `reboot -f` via QMP sendkey, passes iff the
  controller re-attaches and the second boot reaches a shell.  Exercises
  the [RF-5] shutdown hooks.
- `torture_unplug.py {ehci|xhci|uhci}` — `device_del` a usb-storage disk
  mid-`dd`; passes iff the disconnect propagates, nothing panics, and a
  typed `echo ALIVE` lands after teardown.  Exercises RF-1a/RF-4 error
  classification and the disconnect paths.

Notes: QMP `sendkey` first-keystroke tends to double (poll warm-up); the
typers send a sacrificial key + ctrl-u first.  Typing while a yanked
device's retries hold `submit_lock` starves the keyboard on the same
controller -- wait for `usb_msc: detached device` before typing.
