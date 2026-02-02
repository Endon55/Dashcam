# Cross-compile to Raspberry Pi 5 (aarch64) using a Pi sysroot

Overview
- Copy a representative sysroot (root filesystem) from the Pi to your host and use a CMake toolchain that points at that sysroot. The produced binary will link against the Pi's glibc version.

Copy sysroot from the Pi (recommended)
1. On your host, create a destination directory for the sysroot:

```bash
mkdir -p /home/$USER/pi-sysroot
```

2. Use `rsync` over SSH to copy the Pi root filesystem (run from the host). This example uses `--rsync-path="sudo rsync"` so the Pi uses sudo to access protected files.

```bash
rsync -aAX --delete \
  --exclude={"/dev/*","/proc/*","/sys/*","/tmp/*","/run/*","/mnt/*","/media/*","/lost+found"} \
  --rsync-path="sudo rsync" \
  pi@raspberrypi.local:/ /home/$USER/pi-sysroot/
```

Replace `pi@raspberrypi.local` with your Pi's user/host.

Build on host using the sysroot
1. Use the included helper script (it calls CMake with the toolchain file):

```bash
./scripts/build-cross.sh /home/$USER/pi-sysroot
```

2. Or run CMake manually:

```bash
cmake -S . -B build-aarch64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-pi-toolchain.cmake \
  -DCMAKE_SYSROOT=/home/$USER/pi-sysroot \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-aarch64 -- -j$(nproc)
```

Notes and troubleshooting
- Ensure `gcc-aarch64-linux-gnu` and `g++-aarch64-linux-gnu` are installed on the host:

```bash
sudo apt-get update && sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

- If a library on the Pi is in a nonstandard location, set `CMAKE_PREFIX_PATH` or pass additional `-DCMAKE_EXE_LINKER_FLAGS`.
- For pkg-config-aware packages, ensure `PKG_CONFIG_LIBDIR` points into the sysroot's pkgconfig dirs (the toolchain sets this environment variable).

If you want, I can:
- help rsync the sysroot (I can't run the command from here but I can generate the exact command for your environment), or
- adjust the toolchain to integrate vcpkg or other dependencies you use.
