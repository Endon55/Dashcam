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
sudo apt-get install -y build-essential git make \
pkg-config cmake ninja-build gnome-desktop-testing libasound2-dev libpulse-dev \
libaudio-dev libfribidi-dev libjack-dev libsndio-dev libx11-dev libxext-dev \
libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libthai-dev libusb-1.0-0-dev \
libwayland-dev libxkbcommon-dev libegl1-mesa-dev nasm autoconf autoconf-archive automake \
libtool

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

