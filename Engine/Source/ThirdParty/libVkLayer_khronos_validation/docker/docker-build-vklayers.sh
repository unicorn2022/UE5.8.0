#!/bin/bash

set -v

## Copyright Epic Games, Inc. All Rights Reserved.

# Should be run in docker image, launched something like this (see RunMe.sh script):
#


DISTRO=${1:-Rocky8}

if [ $UID -eq 0 ]; then

	if [ ${DISTRO} == "Rocky8" ]; then
		yum install -y epel-release
		yum install -y gcc gcc-c++ git-core make cmake \
			alsa-lib-devel pulseaudio-libs-devel pipewire-devel libX11-devel \
			libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel \
			libXi-devel libXScrnSaver-devel dbus-devel systemd-devel \
			mesa-libGL-devel libxkbcommon-devel mesa-libGLES-devel \
			mesa-libEGL-devel vulkan-devel wayland-devel wayland-protocols-devel libdrm-devel python3.11 clang-20.1.8 lld

	else
		echo Unsupported distro ${DISTRO}
		exit 1
	fi

	# Create non-privileged user and workspace
	adduser buildmaster
	mkdir -p /build
	chown buildmaster:nobody -R /build /VkLayer
	cd /build

	exec su buildmaster "$0"
fi

# This will be run from user buildmaster

# useful if building for other architectures in the future.  At present only x86_64-unknown-linux-gnu is supported
export ARCH=$(uname -m)

# Get num of cores
export CORES=$(getconf _NPROCESSORS_ONLN)
echo Using ${CORES} cores for building

set -e

cd /VkLayer
cmake -S . -B /build -DBUILD_WERROR=OFF -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DUPDATE_DEPS=ON -DCMAKE_CXX_FLAGS="-nostdinc++ -I/multiarch/x86_64-unknown-linux-gnu/include/c++/v1" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld -nodefaultlibs /multiarch/x86_64-unknown-linux-gnu/lib64/libc++.a /multiarch/x86_64-unknown-linux-gnu/lib64/libc++abi.a -lm -lc -lgcc_s" -DCMAKE_TOOLCHAIN_FILE=/src/toolchain.cmake

cmake --build /build --config Release -j

set +e
