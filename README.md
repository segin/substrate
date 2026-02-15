# Substrate Kernel

Substrate is a modern, monolithic Unix-like operating system kernel targeting x86 architectures. It provides a robust foundation for building high-performance systems with a focus on simplicity, portability, and native personality emulation.

## Key Features
- **Multilevel Feedback Queue Scheduler**: Supports Realtime, Timeshare, and Idle classes with SMP load balancing.
- **Advanced Memory Management**: Buddy allocator for physical memory, recursive paging for virtual memory, and UMA slab allocator for kernel objects.
- **VFS Layer**: Unified abstraction for multiple filesystems (Ext2, FAT, Minix, UDF).
- **Personality Emulation**: Native support for running Linux and FreeBSD binaries via syscall translation.
- **POSIX Compliance**: Aiming for high compatibility with standard Unix utilities and libraries.

## Getting Started

### Prerequisites
- GCC with `-m32` support.
- Make.
- QEMU (for testing).

### Building the System
To build the kernel and base userland:
```bash
make
```

### Running the System
To launch the kernel in QEMU:
```bash
./run.sh
```

## Documentation

- **[Architecture](ARCHITECTURE.md)**: High-level system overview and subsystem design.
- **[Component Registry](COMPONENTS.md)**: Status and verification tracking for all system components.
- **[Specifications](docs/specs/)**: Detailed technical specifications for drivers, filesystems, and kernel subsystems.
- **[Tasks](TASKS.md)**: Roadmap and current development status.

## Contributing
Please refer to **[AGENTS.md](AGENTS.md)** for development guidelines and standards.
