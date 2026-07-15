#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds libvorbis, libvorbisenc, and libvorbisfile for Linux (x86_64 and
# aarch64), producing both standard and fPIC variants for each architecture
# using UAT's BuildCMakeLib.
#
# Depends on libogg headers from Engine/Source/ThirdParty/Ogg.

set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P)
VORBIS_ROOT=$(realpath -s "$SCRIPT_DIR/../..")
VORBIS_VERSION=$(basename "$VORBIS_ROOT")
ENGINE_ROOT=$(realpath -s "$SCRIPT_DIR/../../../../../..")

# Set up the UE Linux cross-compilation toolchain
if [[ -z "$LINUX_MULTIARCH_ROOT" ]]; then
	if [[ -z "$UE_SDKS_ROOT" ]]; then
		SDK_VERSION=$(python3 "$ENGINE_ROOT/Build/BatchFiles/Linux/GetLinuxSDKVersion.py")
		export LINUX_MULTIARCH_ROOT="$ENGINE_ROOT/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/$SDK_VERSION"
	else
		source "$UE_SDKS_ROOT/HostLinux/Linux_x64/OutputEnvVars.txt"
	fi
fi

if [[ ! -d "$LINUX_MULTIARCH_ROOT" ]]; then
	echo "ERROR: LINUX_MULTIARCH_ROOT not found: $LINUX_MULTIARCH_ROOT"
	echo "Install the UE Linux toolchain or set LINUX_MULTIARCH_ROOT manually."
	exit 1
fi
echo "Using toolchain: $LINUX_MULTIARCH_ROOT"

# Ogg dependency - vorbis needs ogg headers and library for linking
OGG_ROOT="$ENGINE_ROOT/Source/ThirdParty/Ogg/libogg-1.2.2"

ARCHS=(
	"x86_64-unknown-linux-gnu"
	"aarch64-unknown-linux-gnueabi"
)

# The three libraries produced by the vorbis CMakeLists.txt
VORBIS_LIBS=("vorbis" "vorbisenc" "vorbisfile")

BuildVorbis()
{
	local ARCH=$1
	local USE_FPIC=$2
	local OGG_LIB="$OGG_ROOT/lib/Unix/$ARCH/libogg.a"
	local CMAKE_ARGS="-DOGG_INCLUDE_DIRS=$OGG_ROOT/include -DOGG_LIBRARIES=$OGG_LIB"

	if [[ "$USE_FPIC" == "true" ]]; then
		CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
	fi

	echo "========================================="
	echo "Building libvorbis for $ARCH (fPIC=$USE_FPIC)"
	echo "========================================="

	"$ENGINE_ROOT/Build/BatchFiles/RunUAT.sh" BuildCMakeLib \
		-TargetPlatform=Unix \
		-TargetArchitecture="$ARCH" \
		-TargetLib=Vorbis \
		-TargetLibVersion="$VORBIS_VERSION" \
		-TargetConfigs=Release \
		-LibOutputPath=lib \
		-CMakeGenerator=Makefile \
		-CMakeAdditionalArguments="$CMAKE_ARGS" \
		-SkipCreateChangelist

	# Move each library from lib/Unix/<arch>/Release/ to lib/Unix/<arch>/
	local DEST_DIR="$VORBIS_ROOT/lib/Unix/$ARCH"
	mkdir -p "$DEST_DIR"

	for LIB in "${VORBIS_LIBS[@]}"; do
		local BUILD_OUTPUT="$VORBIS_ROOT/lib/Unix/$ARCH/Release/lib${LIB}.a"

		if [[ ! -f "$BUILD_OUTPUT" ]]; then
			echo "ERROR: Expected output not found: $BUILD_OUTPUT"
			exit 1
		fi

		if [[ "$USE_FPIC" == "true" ]]; then
			mv "$BUILD_OUTPUT" "$DEST_DIR/lib${LIB}_fPIC.a"
			echo "Installed: $DEST_DIR/lib${LIB}_fPIC.a"
		else
			mv "$BUILD_OUTPUT" "$DEST_DIR/lib${LIB}.a"
			echo "Installed: $DEST_DIR/lib${LIB}.a"
		fi
	done

	# Clean up build artifacts
	rm -rf "$VORBIS_ROOT/lib/Unix/$ARCH/Release"
	rm -rf "$VORBIS_ROOT/Intermediate/Unix/$ARCH"
}

for ARCH in "${ARCHS[@]}"; do
	BuildVorbis "$ARCH" "false"
	BuildVorbis "$ARCH" "true"
done

echo ""
echo "Done. Built libraries:"
for ARCH in "${ARCHS[@]}"; do
	ls -la "$VORBIS_ROOT/lib/Unix/$ARCH/"lib*.a
done
