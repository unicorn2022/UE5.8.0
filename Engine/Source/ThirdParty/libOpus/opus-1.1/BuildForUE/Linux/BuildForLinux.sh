#!/bin/bash

# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds libopus and libresampler for Linux (x86_64 and aarch64),
# producing both standard and fPIC variants for each architecture
# using UAT's BuildCMakeLib.

set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P)
OPUS_ROOT=$(realpath -s "$SCRIPT_DIR/../..")
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

# BuildCMakeLib uses -TargetLib to find the source under ThirdParty.
# Since our CMakeLists.txt files are in BuildForUE/ (not the opus-1.1 root),
# we use -TargetLibSourcePath to point at them.
OPUS_CMAKE_DIR="$OPUS_ROOT/BuildForUE/opus"
RESAMPLER_CMAKE_DIR="$OPUS_ROOT/BuildForUE/resampler"

BuildLib()
{
	local LIB_NAME=$1
	local ARCH=$2
	local USE_FPIC=$3
	local CMAKE_FILE=$4
	local CMAKE_ARGS=""

	if [[ "$USE_FPIC" == "true" ]]; then
		CMAKE_ARGS="-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
	fi

	echo "========================================="
	echo "Building $LIB_NAME for $ARCH (fPIC=$USE_FPIC)"
	echo "========================================="

	"$ENGINE_ROOT/Build/BatchFiles/RunUAT.sh" BuildCMakeLib \
		-TargetPlatform=Unix \
		-TargetArchitecture="$ARCH" \
		-TargetLib=libOpus \
		-TargetLibVersion=opus-1.1 \
		-TargetLibSourcePath="$CMAKE_FILE" \
		-TargetConfigs=Release \
		-LibOutputPath=lib \
		-CMakeGenerator=Makefile \
		-CMakeAdditionalArguments="$CMAKE_ARGS" \
		-SkipCreateChangelist

	# BuildCMakeLib outputs to lib/Unix/<arch>/Release/lib<name>.a
	# but libOpus.build.cs expects Unix/<arch>/lib<name>[_fPIC].a
	local BUILD_OUTPUT="$OPUS_ROOT/lib/Unix/$ARCH/Release/lib${LIB_NAME}.a"
	local DEST_DIR="$OPUS_ROOT/Unix/$ARCH"

	if [[ ! -f "$BUILD_OUTPUT" ]]; then
		echo "ERROR: Expected output not found: $BUILD_OUTPUT"
		exit 1
	fi

	mkdir -p "$DEST_DIR"

	if [[ "$USE_FPIC" == "true" ]]; then
		mv "$BUILD_OUTPUT" "$DEST_DIR/lib${LIB_NAME}_fPIC.a"
		echo "Installed: $DEST_DIR/lib${LIB_NAME}_fPIC.a"
	else
		mv "$BUILD_OUTPUT" "$DEST_DIR/lib${LIB_NAME}.a"
		echo "Installed: $DEST_DIR/lib${LIB_NAME}.a"
	fi

	# Clean up build artifacts
	rm -rf "$OPUS_ROOT/lib/Unix/$ARCH/Release"
	rm -rf "$OPUS_ROOT/Intermediate/Unix/$ARCH"
}

for ARCH in "${ARCHS[@]}"; do
	BuildLib "opus" "$ARCH" "false" "$OPUS_CMAKE_DIR"
	BuildLib "opus" "$ARCH" "true"  "$OPUS_CMAKE_DIR"
	BuildLib "resampler" "$ARCH" "false" "$RESAMPLER_CMAKE_DIR"
	BuildLib "resampler" "$ARCH" "true"  "$RESAMPLER_CMAKE_DIR"
done

# Clean up the lib/Unix directory created by BuildCMakeLib (we use Unix/ directly)
rm -rf "$OPUS_ROOT/lib/Unix"

echo ""
echo "Done. Built libraries:"
for ARCH in "${ARCHS[@]}"; do
	ls -la "$OPUS_ROOT/Unix/$ARCH/"lib*.a
done
