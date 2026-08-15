# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- User-mode crash detection with 10-second heartbeat timeout
- File preallocation support (`--preallocate` option with `SetFileValidData`)
- Event wait mode for low CPU usage in vnvme-server
- Graceful shutdown with `WdfIoQueueStop` in kernel driver
- v2 modular architecture enabled as default for vnvme-server
- Comprehensive documentation restructuring

### Changed
- Heartbeat timeout increased from 5 seconds to 10 seconds
- Unified code style: separator comments now use `//===` format
- Renamed `SharedMemory` to `Shm` throughout codebase
- Documentation reorganized into categorized subdirectories

### Fixed
- Queue stop handling during D0Exit power state transition
- Control queue saved for graceful shutdown

## [0.1.0] - 2024-12-23

### Added
- Initial implementation of virtual NVMe controller emulator
- Kernel driver (`vnvme.sys`) with FDO/PDO architecture
- User-mode service (`vnvme-server.exe`) with command processing
- Management tool (`vnvmectl.exe`) for controller operations
- BAR0 memory emulation with real physical memory
- PCIe configuration space emulation
- Doorbell polling engine with high-precision timer
- Shared memory communication between kernel and user-mode
- PRP parsing and data copy functionality
- Admin commands: Identify, Create/Delete I/O Queue, Get/Set Features, Abort
- I/O commands: Read, Write, Flush
- Memory and File storage backends
- Dual-mode architecture (kernel-mode and user-mode command processing)
- Comprehensive documentation

### Technical Details
- WDK: 10.0.26100.0
- KMDF: 1.15
- C17 (kernel), C++23 (user-mode)
- Windows 10 20H1+ / Windows 11 / Windows Server 2019+

---

[Unreleased]: https://github.com/user/virtual-nvme-driver/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/user/virtual-nvme-driver/releases/tag/v0.1.0
