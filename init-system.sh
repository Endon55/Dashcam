#!/bin/bash

set -euo pipefail

dashcam_dir=$(X= cd -- "$(dirname -- "$0")" && pwd -P)

vcpkg_repo="https://github.com/microsoft/vcpkg.git"
vcpkg_dir="$HOME/vcpkg"
vcpkg_script="bootstrap-vcpkg.sh"

if [ ! -d "$vcpkg_dir/.git" ]; then
    git clone "$vcpkg_repo" "$vcpkg_dir"
fi

cd "$vcpkg_dir"
chmod a+x "$vcpkg_script"

. "$vcpkg_script"

sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libudev-dev \
    nasm \
    ninja-build \
    autoconf \
    autoconf-archive \
    automake \
    libtool \
    libxtst-dev

VCPKG_ROOT_LINE="export VCPKG_ROOT=\"$vcpkg_dir\""
PATH_LINE="export PATH=\"\$VCPKG_ROOT:\$PATH\""

# 4. Safely append to ~/.bashrc if they don't already exist
echo "Configuring environment variables..."

if ! grep -Fq "$VCPKG_ROOT_LINE" "$HOME/.bashrc"; then
    echo "" >> "$HOME/.bashrc"
    echo "# vcpkg configuration" >> "$HOME/.bashrc"
    echo "$VCPKG_ROOT_LINE" >> "$HOME/.bashrc"
    echo "Added VCPKG_ROOT to ~/.bashrc"
else
    echo "VCPKG_ROOT configuration already exists in ~/.bashrc"
fi

if ! grep -Fq "PATH=\"\$VCPKG_ROOT:\$PATH\"" "$HOME/.bashrc"; then
    echo "$PATH_LINE" >> "$HOME/.bashrc"
    echo "Added vcpkg to PATH in ~/.bashrc"
else
    echo "vcpkg PATH configuration already exists in ~/.bashrc"
fi

# Set vars for current shell as well. Some .bashrc files are interactive-only.
export VCPKG_ROOT="$vcpkg_dir"
export PATH="$VCPKG_ROOT:$PATH"

cd "$dashcam_dir"

if command -v ninja >/dev/null 2>&1; then
    preset="host-debug"
    build_dir="build/debug"
else
    preset="host-debug-make"
    build_dir="build/debug-make"
fi

echo "Configuring with preset: $preset"
cmake --preset "$preset" --fresh
cmake --build "$build_dir"

