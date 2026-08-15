# Virtual NVMe Controller Emulator

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()

A **Windows software NVMe controller emulator** that creates real virtual NVMe devices using a hybrid user-mode/kernel-mode architecture. The Windows native NVMe driver (stornvme.sys) recognizes and uses these virtual devices.

## ✨ Features

- **Real NVMe Device** - Appears as NVMe controller in Device Manager
- **Native Driver Compatible** - Works with Windows stornvme.sys
- **NVMe Tool Support** - nvme-cli, Crystal Disk Info compatible
- **SPDK-like Architecture** - User-mode business logic, minimal kernel-mode
- **Flexible Backends** - Memory, File, VHD storage backends
- **Developer Friendly** - User-mode debugging, no BSOD on crash

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      User Mode                              │
│  ┌────────────────────────────────────────────────────────┐│
│  │                vnvme-server.exe                        ││
│  │  [Command Engine] [Backends] [Management] [Logging]    ││
│  │                      │                                 ││
│  │     Shared Memory: [Control│SQ/CQ│Data Buffer]         ││
│  └──────────────────────│─────────────────────────────────┘│
├─────────────────────────│───────────────────────────────────┤
│                      Kernel Mode                            │
│  ┌──────────────────────▼─────────────────────────────────┐│
│  │                   vnvme.sys                            ││
│  │  [Bus Mgmt] [BAR0 Emulation] [Doorbell Polling]        ││
│  │  [PRP Parse] [Shared Memory] [Completion]              ││
│  └────────────────────────────────────────────────────────┘│
│  ┌────────────────────────────────────────────────────────┐│
│  │              stornvme.sys (Windows Native)             ││
│  └────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

## 🚀 Quick Start

### Requirements

- Windows 10 20H1+ / Windows 11 / Windows Server 2019+
- Visual Studio 2022
- Windows Driver Kit (WDK) 10.0.26100+
- Test signing mode enabled

### Build

```powershell
# Clone the repository
git clone <repository-url>
cd virtual-nvme-driver

# Build with MSBuild
msbuild vnvme.sln /p:Configuration=Release /p:Platform=x64
```

### Install

```powershell
# Enable test signing (requires admin, reboot required)
bcdedit /set testsigning on

# Install driver
pnputil /add-driver vnvme.inf /install

# Start user-mode service
vnvme-server.exe --size 100G --backend file --file C:\vnvme\disk.img
```

### Manage

```powershell
vnvmectl status    # Check driver and service status
vnvmectl list      # List virtual controllers
vnvmectl create --size 100G --backend memory
```

## 📁 Project Structure

```
virtual-nvme-driver/
├── vnvme/              # Kernel driver (vnvme.sys)
├── vnvme-server/       # User-mode service (vnvme-server.exe)
├── vnvmectl/           # Management CLI (vnvmectl.exe)
├── include/            # Shared headers
├── docs/               # Documentation
└── scripts/            # Build and utility scripts
```

## 📚 Documentation

See [docs/README.md](docs/README.md) for comprehensive documentation:

- [Architecture Design](docs/architecture/overview.md)
- [Build Guide](docs/development/build-guide.md)
- [User Manual](docs/operations/user-manual.md)
- [API Reference](docs/reference/api/api-reference.md)
- [Debugging Guide](docs/development/debugging.md)

##  Development

```powershell
# Debug build
msbuild vnvme.sln /p:Configuration=Debug /p:Platform=x64

# Run tests
.\build\Debug\x64\vnvme-tests.exe
```

See [CODING-STANDARDS.md](docs/CODING-STANDARDS.md) for coding guidelines.

## 📄 License

MIT License - see LICENSE file for details.

## 🤝 Contributing

Contributions welcome! Please read the documentation and follow the coding standards.
