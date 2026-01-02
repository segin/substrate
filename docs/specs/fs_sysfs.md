# SysFS Specification

## Overview
SysFS is a virtual filesystem that exposes the kernel's object hierarchy (KObjects) to userspace. it is typically mounted at `/sys` and provides information about buses, drivers, and devices.

## Implementation
- **KObjects:** The base structure for all kernel objects. Each KObject can have a parent, forming a tree.
- **KSets:** Containers for groups of KObjects.
- **Dynamic Representation:**
    - `/sys/bus/`: Contains subdirectories for each hardware bus (PCI, USB, etc.).
    - `/sys/class/`: Contains subdirectories for device classes (input, net, storage).
    - `/sys/devices/`: Contains the full hierarchical tree of all devices.

## VFS Integration
- Registered as a virtual filesystem.
- Content is generated based on the internal KObject tree.

## API
### `void kobject_init(struct kobject *kobj, const char *name)`
Initializes a new kernel object.

### `void kset_init(struct kset *kset, const char *name)`
Initializes a new kernel set.

## Constraints
- Hierarchy is currently static in the initial prototype.
- No support for hotplug notifications via SysFS.
- Attribute reading/writing is not yet implemented.
