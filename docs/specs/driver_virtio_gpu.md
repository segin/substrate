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

Resource creation, scanout binding, dirty-region flush, display-info queries,
and cursor image handling are separate follow-on steps.

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
