# SCSI Driver Model

## Scope

The SCSI storage stack is split into three layers:

- `scsi.c`: transport-independent mid-layer
- `scsi_ctl.c`: devfs/ioctl control surface
- `scsi_dev.c`: high-level block-device publication and media policy

Transport/HBA drivers bind underneath this stack through `scsi_link_t`. Current
in-tree users are the ATAPI bridge and the generic SCSI core. Other transports
such as USB mass storage or VirtIO-SCSI are expected to plug into the same
`scsi_link_t.execute` contract.

## Object Model

### `scsi_link_t`

Represents one transport adapter or bus attachment point.

Important fields:

- `name`: transport name (`atapi0`, `scsi0`, ...)
- `bus_id`: controller-local bus number exposed in `/dev/storage/scsi/B`
- `max_targets`
- `max_luns`
- `adapter_queue_depth`
- `flags`
- `execute`: required command-dispatch callback
- `reset_device`
- `reset_bus`
- `priv`

Registration flow:

1. `scsi_register_link()`
2. create `/dev/storage/scsi/B` bus node
3. scan the bus immediately with `scsi_scan_bus()`

Unregister flow:

1. detach high-level devices that belong to the link
2. remove them from the device registry
3. drop the link from the transport registry

### `scsi_device_t`

Represents one `Bus:Target:LUN` addressable device.

Important fields:

- addressing: `bus`, `target`, `lun`
- identity: `type`, `vendor`, `product`, `revision`, `scsi_version`
- capacity: `capacity`, `sector_size`
- state: `flags`, `online`, `media_present`, `removable`, `write_protected`
- queueing: `queue_depth`, `max_queue_depth`
- transport binding: `link`
- publication identity: `device_num`

Registry rule:

- `bus:target:lun` must be unique

### `scsi_request_t`

Represents one command execution context.

Important fields:

- `cdb`, `cdb_len`
- `data`, `data_len`, `data_xfer`
- `sense`, `sense_len`
- `status`, `state`, `error`
- `timeout_ms`, `retries`, `max_retries`
- `callback`, `callback_arg`

Pool rule:

- requests are allocated from a fixed pool and returned to the free list on
  completion/free

## Mid-Layer Execution Contract

### `scsi_execute_sync()`

Synchronous helpers build a request, set direction flags, dispatch through the
 owning `scsi_link_t.execute` callback, and apply retry/sense policy.

Retry behavior currently distinguishes:

- `CHECK CONDITION` with auto-sense
- `UNIT ATTENTION`
- `NOT READY`
- `ABORTED COMMAND`
- `BUSY`
- `TASK SET FULL`
- `MEDIUM ERROR`

Sense helpers:

- `scsi_sense_key()`
- `scsi_sense_asc()`
- `scsi_sense_ascq()`
- `scsi_sense_string()`

Byte-order helpers:

- `scsi_be16()`
- `scsi_be32()`
- `scsi_put_be16()`
- `scsi_put_be32()`

## CDB Helpers

In-tree helpers currently cover:

- `TEST UNIT READY`
- `INQUIRY`
- `REQUEST SENSE`
- `READ CAPACITY (10/16)`
- `READ (10/16)`
- `WRITE (10/16)`
- `REPORT LUNS`
- `SYNCHRONIZE CACHE`

The mid-layer emits big-endian CDB fields through the `scsi_put_be*()` helpers.

## Device Node Hierarchy

Published nodes:

- `/dev/storage/scsi/B:T:L`
  - generic passthrough node
  - supports raw SCSI ioctl dispatch
- `/dev/storage/scsi/B`
  - bus control node
  - supports rescan/count/reset-style control ioctls
- `/dev/storage/scsiN`
  - high-level block alias for block-capable devices

High-level block publication is intentionally policy-bearing:

- disks publish as 512-byte block devices unless the transport reports
  otherwise
- CD-ROM / optical media publish with 2048-byte sectors
- WORM devices are published through the same block path but reject generic
  overwrite writes
- detach issues `SYNCHRONIZE CACHE` for write-capable block devices

## Ioctl Surface

Bus/controller side:

- `SCSI_IOCTL_SCAN_BUS`
- `SCSI_IOCTL_GET_COUNT`
- `SCSI_IOCTL_RESET_BUS`

Generic device side:

- `SCSI_IOCTL_GET_INFO`
- `SCSI_IOCTL_GET_IDLUN`
- `SCSI_IOCTL_SEND_CMD`

Raw command execution is root-only and copies request/response payloads through
kernel-owned buffers before dispatch.
