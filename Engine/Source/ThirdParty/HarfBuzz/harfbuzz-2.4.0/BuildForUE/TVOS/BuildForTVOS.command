#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds HarfBuzz 2.4.0 static libraries for tvOS (device + simulator), recompiled
# against ICU 78 headers. Outputs to:
#   harfbuzz-2.4.0/lib-icu78/TVOS/{Debug,Release}/libharfbuzz{,d}.a
#   harfbuzz-2.4.0/lib-icu78/TVOS/Simulator/{Debug,Release}/libharfbuzz{,d}.a

ENGINE_ROOT=${0:a:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty

TOOLCHAIN_FILE=${ENGINE_ROOT}/Source/ThirdParty/CMake/PlatformScripts/TVOS/TVOS.cmake
HB_TVOS_VERSION=harfbuzz-2.4.0

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=harfbuzz

for ARCH in arm64 x86_64 tvossimulator;
do
  "${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=TVOS -TargetArchitecture=${ARCH} -TargetLib=HarfBuzz -TargetLibVersion=${HB_TVOS_VERSION} -TargetConfigs=Debug+Release -LibOutputPath=lib-icu78 -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -MakeTarget=${MAKE_TARGET} -SkipCreateChangelist
done

pushd "${ENGINE_ROOT}/Source/ThirdParty/HarfBuzz/${HB_TVOS_VERSION}/lib-icu78/TVOS"
mkdir -p Debug Release Simulator Simulator/Debug Simulator/Release
mv arm64/Debug/libharfbuzz.a Debug/libharfbuzz.a
mv arm64/Release/libharfbuzz.a Release/libharfbuzz.a
lipo -create tvossimulator/Debug/libharfbuzz.a x86_64/Debug/libharfbuzz.a -output Simulator/Debug/libharfbuzz.a
lipo -create tvossimulator/Release/libharfbuzz.a x86_64/Release/libharfbuzz.a -output Simulator/Release/libharfbuzz.a
rm -rf arm64 x86_64 tvossimulator
popd
