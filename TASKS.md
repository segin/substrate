# TASKS.md

This file is now the task index. The monolithic task list was refactored into section files under `docs/tasks/`.
All checklist items in the refactored files are intentionally reset to open (`[ ]`) as the new planning baseline.

## Kernel/ld.so ABI Documentation Tracking

- [ ] Keep Linux ABI contract current: `docs/kernel-ldso-abi-linux.md`.
- [ ] Keep FreeBSD ABI contract current: `docs/kernel-ldso-abi-freebsd.md`.
- [ ] Keep Substrate ABI contract current: `docs/kernel-ldso-abi-substrate.md`.

## Refactored Task Sections

- [ ] [01. 1. Kernel Core (`sys/core`, `sys/kern`)](docs/tasks/01-1-kernel-core.md) (975 checklist items)
- [ ] [02. 2. Architecture (`sys/arch`)](docs/tasks/02-2-architecture.md) (74 checklist items)
- [ ] [03. 3. Drivers (`sys/drivers`)](docs/tasks/03-3-drivers.md) (1149 checklist items)
- [ ] [04. 4. Filesystem (`sys/fs`, `sys/vfs`)](docs/tasks/04-4-filesystem.md) (361 checklist items)
- [ ] [05. 5. System Calls & Personalities](docs/tasks/05-5-system-calls-and-personalities.md) (672 checklist items)
- [ ] [06. 6. C Library (`lib/c`)](docs/tasks/06-6-c-library.md) (1237 checklist items)
- [ ] [07. 6a. System Call Wrapper Library (`lib/sys`)](docs/tasks/07-6a-system-call-wrapper-library.md) (52 checklist items)
- [ ] [08. 6b. Editline Library (`lib/edit`)](docs/tasks/08-6b-editline-library.md) (403 checklist items)
- [ ] [09. 7. Userland Binaries (`bin/`)](docs/tasks/09-7-userland-binaries.md) (2622 checklist items)
- [ ] [10. 8. LibC & Build System (User Requests & Audit)](docs/tasks/10-8-libc-and-build-system.md) (451 checklist items)
- [ ] [11. 7. Core Utilities (`bin/`)](docs/tasks/11-7-core-utilities.md) (16 checklist items)
- [ ] [12. 8. Security and Identity](docs/tasks/12-8-security-and-identity.md) (30 checklist items)
- [ ] [13. 9. Networking (Future)](docs/tasks/13-9-networking.md) (81 checklist items)
- [ ] [14. 10. Hardware Support & Peripherals](docs/tasks/14-10-hardware-support-and-peripherals.md) (118 checklist items)
- [ ] [15. 11. Build System & Packaging](docs/tasks/15-11-build-system-and-packaging.md) (6 checklist items)
- [ ] [16. 12. Continuous Directives](docs/tasks/16-12-continuous-directives.md) (7 checklist items)
- [ ] [17. 13. Bus Enumeration & Driver Model](docs/tasks/17-13-bus-enumeration-and-driver-model.md) (135 checklist items)
- [ ] [18. 14. Native 64-bit Stat ABI Enforcement](docs/tasks/18-14-native-64-bit-stat-abi-enforcement.md) (23 checklist items)
- [ ] [19. 15. Personality Driver Audit & Refactor](docs/tasks/19-15-personality-driver-audit-and-refactor.md) (29 checklist items)
- [ ] [20. 16. ELKS Kernel Personality (16-bit LDT-based Execution)](docs/tasks/20-16-elks-kernel-personality.md) (66 checklist items)
- [ ] [21. 6. Userspace/Tools (`usr.bin/yacc`)](docs/tasks/21-6-userspace-tools.md) (28 checklist items)

## Notes

- Each section file contains:
  - A reimplemented checklist (all open)
  - User stories for every checklist item
  - INCOSE/EARS-form requirements for every checklist item
- File lineage for each section was established by rename+restore fork-copy commits from `TASKS.md`.
