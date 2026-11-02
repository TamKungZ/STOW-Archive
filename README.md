# STOW Archive

A small cross-platform C++ UI starter that uses each platform's native UI API:

- Windows: Win32
- macOS: Cocoa
- Linux: GTK 4

The UI implementations are platform-specific by design; there is no shared UI
abstraction layer.

## Build

Requirements:

- CMake 3.21 or newer
- A C++20 compiler
- Windows: Visual Studio 2022 or another Windows SDK toolchain
- macOS: Xcode Command Line Tools
- Linux: GTK 4 development files and `pkg-config`

Configure and build from the project root:

```powershell
cmake -S . -B build
cmake --build build
```

On Linux, install the GTK 4 development package provided by your distribution
before configuring CMake.

## Repository submodule

The `STOW` directory is the `TamKungZ/STOW` Git submodule. After cloning this
project, initialize it with:

```sh
git submodule update --init --recursive
```
