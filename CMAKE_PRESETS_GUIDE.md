# CMake Presets Guide

This project supports multiple CMake build configurations that allow you to easily switch between building for your host machine and cross-compiling for a Raspberry Pi.

## Available Presets

### Host Machine Builds
These presets build for your x64-linux host machine:

- **`host-release`** - Release build for host machine (optimized, no debug symbols)
- **`host-debug`** - Debug build for host machine (debug symbols, development-friendly)

### Raspberry Pi Cross-Compilation
These presets cross-compile for ARM64 Raspberry Pi using your pi-sysroot:

- **`pi-release`** - Release build for Raspberry Pi (optimized, no debug symbols)
- **`pi-debug`** - Debug build for Raspberry Pi (debug symbols, development-friendly)

## Usage

### Configure and Build for Host Machine (Debug)

```bash
cmake --preset host-debug
cmake --build build/debug
```

### Configure and Build for Host Machine (Release)

```bash
cmake --preset host-release
cmake --build build/release
```

### Configure and Build for Raspberry Pi (Debug)

```bash
cmake --preset pi-debug
cmake --build build/debug/arm
```

### Configure and Build for Raspberry Pi (Release)

```bash
cmake --preset pi-release
cmake --build build/release/arm
```

## List All Available Presets

```bash
cmake --list-presets
```

## Build Output Directories

Build outputs are organized by configuration:
- `build/debug/` - Host debug build
- `build/release/` - Host release build
- `build/debug/arm/` - Raspberry Pi debug build
- `build/release/arm/` - Raspberry Pi release build

## VS Code Integration

In VS Code, you can select a preset from the CMake extension's preset picker. This will:
1. Configure the project with the selected preset
2. Update IntelliSense to match the correct toolchain
3. Build with the appropriate configuration

## Key Configuration Details

### Host Builds
- Uses native x64-linux toolchain
- Targets x64-linux triplet
- Includes full development environment

### Raspberry Pi Builds
- Uses ARM64 cross-compiler (`aarch64-linux-gnu-gcc/g++`)
- Targets arm64-linux triplet with pi-sysroot
- CMAKE_SYSROOT is set to `/home/anthony/pi-sysroot`
- Uses vcpkg overlay triplets for ARM64 dependencies

## Troubleshooting

If you encounter issues:

1. **Wrong compiler selected**: Verify you selected the correct preset using `cmake --list-presets`
2. **Missing dependencies**: Ensure vcpkg has installed dependencies for the correct triplet
3. **Sysroot not found**: Check that `/home/anthony/pi-sysroot` exists for Raspberry Pi builds
4. **Clean rebuild**: Remove `build/` directory and reconfigure with `cmake --preset <name>`
