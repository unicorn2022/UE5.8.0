#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds libogg for Linux (x86_64 and aarch64), producing both standard
# and fPIC variants for each architecture using UAT's BuildCMakeLib.

set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P)
OGG_ROOT=$(realpath -s "$SCRIPT_DIR/../..")
OGG_VERSION=$(basename "$OGG_ROOT")
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

ARCHS=(
	"x86_64-unknown-linux-gnu"
	"aarch64-unknown-linux-gnueabi"
)

BuildOgg()
{
	local ARCH=$1
	local USE_FPIC=$2
	local CMAKE_ARGS=""

	if [[ "$USE_FPIC" == "true" ]]; then
		CMAKE_ARGS="-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
	fi

	echo "========================================="
	echo "Building libogg for $ARCH (fPIC=$USE_FPIC)"
	echo "========================================="

	"$ENGINE_ROOT/Build/BatchFiles/RunUAT.sh" BuildCMakeLib \
		-TargetPlatform=Unix \
		-TargetArchitecture="$ARCH" \
		-TargetLib=Ogg \
		-TargetLibVersion="$OGG_VERSION" \
		-TargetConfigs=Release \
		-LibOutputPath=lib \
		-CMakeGenerator=Makefile \
		-CMakeAdditionalArguments="$CMAKE_ARGS" \
		-SkipCreateChangelist

	# BuildCMakeLib outputs to lib/Unix/<arch>/Release/libogg.a
	# but UEOgg.Build.cs expects lib/Unix/<arch>/libogg[_fPIC].a
	local BUILD_OUTPUT="$OGG_ROOT/lib/Unix/$ARCH/Release/libogg.a"
	local DEST_DIR="$OGG_ROOT/lib/Unix/$ARCH"

	if [[ ! -f "$BUILD_OUTPUT" ]]; then
		echo "ERROR: Expected output not found: $BUILD_OUTPUT"
		exit 1
	fi

	if [[ "$USE_FPIC" == "true" ]]; then
		mv "$BUILD_OUTPUT" "$DEST_DIR/libogg_fPIC.a"
		echo "Installed: $DEST_DIR/libogg_fPIC.a"
	else
		mv "$BUILD_OUTPUT" "$DEST_DIR/libogg.a"
		echo "Installed: $DEST_DIR/libogg.a"
	fi

	# Clean up the Release subdirectory and Intermediate build artifacts
	rm -rf "$OGG_ROOT/lib/Unix/$ARCH/Release"
	rm -rf "$OGG_ROOT/Intermediate/Unix/$ARCH"
}

for ARCH in "${ARCHS[@]}"; do
	mkdir -p "$OGG_ROOT/lib/Unix/$ARCH"
	BuildOgg "$ARCH" "false"
	BuildOgg "$ARCH" "true"
done

echo ""
echo "Done. Built libraries:"
for ARCH in "${ARCHS[@]}"; do
	ls -la "$OGG_ROOT/lib/Unix/$ARCH/"libogg*.a
done
