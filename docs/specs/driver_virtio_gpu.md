# VirtIO-GPU Driver

## Scope

The VirtIO-GPU driver provides legacy PCI transport discovery and queue
bring-up for VirtIO GPU adapters. This first stage is intentionally limited to
transport setup:

- match the transitional PCI device
- obtain the legacy I/O BAR
- reset and acknowledge the device
- allocate and register queue `0` (controlq)
- allocate and register queue `1` (cursorq)
- transition the device to `DRIVER_OK`

Dirty-region flush, display-info queries, and cursor image handling are
separate follow-on steps.

## 2D Scanout Resource Bring-Up

The driver can now construct a minimal 2D scanout resource on control queue
`0` by issuing, in order:

- `RESOURCE_CREATE_2D`
- `RESOURCE_ATTACH_BACKING`
- `SET_SCANOUT`

This stage binds resource ID `N` to scanout `0` using a single backing entry
supplied by the caller. It does not transfer pixels to the host.

## Transport Contract

- PCI vendor ID: `0x1af4`
- Transitional device ID: `0x1010`
- Queue `0`: control queue
- Queue `1`: cursor queue

Queue memory is allocated as physically contiguous pages and published through
the legacy `QUEUE_ADDR` register as a PFN.

## Failure Policy

If either required queue is unavailable or cannot be allocated, the driver sets
`VIRTIO_STATUS_FAILED` and leaves the device uninitialized.

## Verification

`tests/sys/host_test_virtio_gpu.c` verifies:

- successful two-queue setup reaches `DRIVER_OK`
- both queue PFNs are written
- missing I/O BAR is rejected
- missing cursor queue is rejected and sets failed status
- 2D scanout creation emits the expected control-queue command sequence
