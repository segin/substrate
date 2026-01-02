# SMP Core Discovery Specification

## Overview
SMP Discovery is the process by which the kernel identifies the number and identifiers of available CPU cores during bootstrap. It supports two primary mechanisms: ACPI MADT (Multiple APIC Description Table) and the legacy Intel MultiProcessor (MP) Spec tables.

## Design
- **ACPI MADT Discovery:**
    1. Locate the RSDP (Root System Description Pointer) in BIOS memory.
    2. Traverse RSDT/XSDT to find the MADT ('APIC' signature).
    3. Parse MADT entries of type 0 (Processor Local APIC) to identify enabled cores.
- **Legacy MP Table Discovery:**
    1. Search for the MP Floating Pointer Structure ('_MP_').
    2. Parse the MP Configuration Table to identify processors.
- **Priority:** ACPI is preferred over legacy MP tables if both are present.
- **Result:** Populate a kernel-internal CPU map (`cpu_info` array).

## API (Internal)
### `void smp_discover_cores(void)`
Primary entry point called during early kernel initialization.

### `int smp_get_cpu_count(void)`
Returns the total number of discovered CPU cores.

## Constraints
- Initial implementation focuses on i386/x86_64 architectures.
- Only identifies cores; initialization (trampolines) is a separate task.
