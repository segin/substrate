# Driver Model and Bus Architecture

## Core Bus Model
- Includes PCI and legacy ISA buses.
- PCI remains optional at runtime.
- Old non-PCI 486-class systems are handled by a fixed-resource ISA probe pass (`isa_probe_legacy()`).

## ISA Bus
- `isa_probe_legacy()` registers standard ISA-era devices:
  - UART (Serial)
  - LPT (Parallel)
  - IDE (Storage)
  - PS/2 (Input)
- Probes include tertiary and quaternary IDE legacy ports.

## PCI Bus
- Late driver registration binds already-enumerated devices immediately.
- Controller families migrated onto the device model work regardless of whether the bus enumerator or the driver registers first.

## VirtIO Family
- No longer performs private PCI rescan during init.
- Block, 9P, and SCSI transports register per-device PCI drivers against the framework-owned PCI device list.
- Bind existing devices through the generic probe/attach path.

## Power Management Model
- The device model owns tree-wide suspend/resume traversal (`device_suspend_all()` / `device_resume_all()`).
- Minimal runtime PM core exists in the device layer with opt-in autosuspend (`device_runtime_enable/get/put/poll()`).
- Provides framework policy but not a separate userspace power daemon or ACPI policy engine.
