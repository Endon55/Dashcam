#!/bin/bash

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

sudo apt install libudev-dev
sudo apt install nasm
sudo apt install ninja-build
sudo apt install autoconf autoconf-archive automake libtool
sudo apt-get install libxtst-dev
sudo apt install libx11-dev libxft-dev libxft-dev
sudo apt install libwayland-dev libxkbcommon-dev libegl1-mesa-dev
sudo apt install libibus-1.0-dev
sudo apt install libxcursor-dev

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

. ~/.bashrc
cd "$dashcam_dir"
cmake --preset default
cmake --build build/debug

