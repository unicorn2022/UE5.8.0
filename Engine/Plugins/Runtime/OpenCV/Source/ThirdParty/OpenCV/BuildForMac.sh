#!/bin/bash
#
# BuildForMac.sh - Build OpenCV as a Universal Binary for macOS
#
# This script builds OpenCV with support for both Apple Silicon (arm64) and Intel (x86_64)
# architectures as a single universal binary.
#
# 1. Download OpenCV source and opencv_contrib modules
#    - OpenCV core library from GitHub
#    - opencv_contrib (includes aruco module required for MetaHuman calibration)
#
# 2. Build for arm64 (Apple Silicon)
#    - Target: macOS 11.0+
#    - Includes Epic's UnrealModules (custom memory allocator integration)
#    - Includes opencv_contrib modules (aruco, tracking, etc.)
#    - Uses UE's bundled zlib/libPNG to avoid SDK conflicts
#    - Disables HDF and freetype modules (not in Windows/Linux builds)
#
# 3. Build for x86_64 (Intel)
#    - Target: macOS 10.15+
#    - Same configuration as arm64
#    - Same configuration as arm64
#
# 4. Create Universal Binary
#    - Combines arm64 and x86_64 binaries using lipo
#    - Sets @rpath install name for dynamic loading
#    - Creates symlink: libopencv_world.dylib -> libopencv_world.$opencv_version.dylib
#
# 5. Install to Plugin Directory
#    - Copies universal binary to Engine/Plugins/Runtime/OpenCV/Binaries/ThirdParty/Mac/
#    - Installs headers to Engine/Plugins/Runtime/OpenCV/Source/ThirdParty/OpenCV/include/
#

set -e

# Specifies the version of opencv to download
opencv_version=4.5.5

# Comment the line below to exclude opencv_contrib from the build
use_opencv_contrib=""

opencv_url=https://github.com/opencv/opencv/archive/"$opencv_version".zip
opencv_src=opencv-"$opencv_version"

opencv_contrib_url=https://github.com/opencv/opencv_contrib/archive/"$opencv_version".zip
opencv_contrib_src=opencv_contrib-"$opencv_version"

# Create build directory
if [ ! -d ./build ]
then
  mkdir build
fi

pushd build

# Download opencv
if [ ! -d "$opencv_src" ]
 then
    if [ ! -f "$opencv_src".zip ]
     then
        echo Downloading "$opencv_url"...
        curl -L -o "$opencv_src".zip "$opencv_url"
    fi

    echo Extracting "$opencv_src".zip...
    unzip "$opencv_src".zip -d .
fi

# Add our module to the path
OPENCV_ROOT_DIRECTORY=`cd $(pwd)/../; pwd`
EXTRA_MODULES_PATH=`cd $(pwd)/../UnrealModules; pwd`

if [ ${use_opencv_contrib+x} ]
then
	# Download opencv_contrib
	if [ ! -d "$opencv_contrib_src" ]
  then
		if [ ! -f "$opencv_contrib_src".zip ]
    then
			echo Downloading "$opencv_contrib_url"...
			curl -L -o "$opencv_contrib_src".zip "$opencv_contrib_url"
		fi
		echo Extracting "$opencv_contrib_src".zip...
		unzip "$opencv_contrib_src".zip -d .
	fi

	# Append it to the extra modules path for opencv to compile in
  CONTRIB_MODULE_PATH=`cd $(pwd)/"$opencv_contrib_src"/modules; pwd`
	EXTRA_MODULES_PATH+=";$CONTRIB_MODULE_PATH"
fi

echo Removing install directories
rm -rf ../include/opencv*
rm -rf ../lib

echo Deleting existing build directories...
rm -rf arm64
rm -rf x86_64
rm -rf universal

mkdir arm64
mkdir x86_64
mkdir universal

UE_ENGINE_LOCATION=`cd $(pwd)/../../../../../../..; pwd`
UE_THIRD_PARTY_LOCATION="$UE_ENGINE_LOCATION/Source/ThirdParty"

ZLIB_INCLUDE_DIR="$UE_THIRD_PARTY_LOCATION/zlib/1.3/include"
ZLIB_LIBRARY="$UE_THIRD_PARTY_LOCATION/zlib/1.3/lib/Mac/Release/libz.a"

PNG_INCLUDE_DIR="$UE_THIRD_PARTY_LOCATION/libPNG/libPNG-1.6.44"
PNG_LIBRARY="$UE_THIRD_PARTY_LOCATION/libPNG/libPNG-1.6.44/lib/Mac/Release/libpng.a"

# Common CMake arguments for both architectures
COMMON_CMAKE_ARGS=(
    -DCMAKE_INSTALL_PREFIX="$OPENCV_ROOT_DIRECTORY"
    -DOPENCV_EXTRA_MODULES_PATH="$EXTRA_MODULES_PATH"
    -DCMAKE_BUILD_TYPE=RELEASE
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_ZLIB=OFF
    -DZLIB_INCLUDE_DIR="$ZLIB_INCLUDE_DIR"
    -DZLIB_LIBRARY="$ZLIB_LIBRARY"
    -DBUILD_PNG=OFF
    -DPNG_FOUND=TRUE
    -DPNG_INCLUDE_DIR="$PNG_INCLUDE_DIR"
    -DPNG_PNG_INCLUDE_DIR="$PNG_INCLUDE_DIR"
    -DPNG_LIBRARY="$PNG_LIBRARY"
    -DPNG_LIBRARIES="$PNG_LIBRARY;$ZLIB_LIBRARY"
    -DCMAKE_IGNORE_PATH="/opt/homebrew;/opt/homebrew/include;/opt/homebrew/lib"
    -DPROTOBUF_UPDATE_FILES=OFF
    -DBUILD_opencv_hdf=OFF
    -DBUILD_opencv_freetype=OFF
    -DBUILD_opencv_dnn=OFF
    -DCMAKE_CXX_FLAGS=""
    -DCMAKE_C_FLAGS=""
)

NUM_CPU=`sysctl -n hw.ncpu`

echo "Building for arm64 (Apple Silicon)..."
pushd arm64

echo Configuring arm64 build...
cmake \
    "${COMMON_CMAKE_ARGS[@]}" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -C "$OPENCV_ROOT_DIRECTORY"/cmake_options.txt \
    ../"$opencv_src"

echo Building OpenCV for arm64...
cmake --build . --config Release -j$NUM_CPU

popd

echo "Building for x86_64 (Intel)..."
pushd x86_64

echo Configuring x86_64 build...
cmake \
    "${COMMON_CMAKE_ARGS[@]}" \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
    -C "$OPENCV_ROOT_DIRECTORY"/cmake_options.txt \
    ../"$opencv_src"

echo Building OpenCV for x86_64...
cmake --build . --config Release -j$NUM_CPU

popd

echo "Creating universal binary..."
pushd universal

ARM64_LIB="../arm64/lib/libopencv_world.$opencv_version.dylib"
X86_64_LIB="../x86_64/lib/libopencv_world.$opencv_version.dylib"
UNIVERSAL_LIB="libopencv_world.$opencv_version.dylib"

echo Combining $ARM64_LIB and $X86_64_LIB into universal binary...
lipo -create "$ARM64_LIB" "$X86_64_LIB" -output "$UNIVERSAL_LIB"

echo Verifying universal binary architectures...
lipo -info "$UNIVERSAL_LIB"

echo Fixing install name for @rpath...
install_name_tool -id "@rpath/$UNIVERSAL_LIB" "$UNIVERSAL_LIB"

echo Creating symlink...
ln -sf "$UNIVERSAL_LIB" libopencv_world.dylib

echo Universal binary size:
ls -lh "$UNIVERSAL_LIB"

popd

echo Moving library to destination folders...

bin_path="$OPENCV_ROOT_DIRECTORY/../../../Binaries/ThirdParty"

echo bin_path is "$bin_path"

if [ ! -d "$bin_path/Mac" ]
then
  mkdir -p "$bin_path/Mac"
fi

echo Copying universal binary to "$bin_path/Mac"...
cp -f universal/libopencv_world*.dylib "$bin_path/Mac/"

echo Installing headers...
cmake --install arm64 --prefix "$OPENCV_ROOT_DIRECTORY"

mv -f "$OPENCV_ROOT_DIRECTORY/include/opencv4/opencv2" "$OPENCV_ROOT_DIRECTORY/include/"

echo Cleaning up...

rm -rf ./arm64
rm -rf ./x86_64
rm -rf ./universal

# build/..
popd

# Remove generated .cmake files
rm -rf OpenCV*.cmake

# Remove unused installed folders
rm -rf ./lib
rm -rf ./include/opencv4
rm -rf ./bin
rm -rf ./share

echo Done. Remember to delete the build directory and submit changed files to p4

echo Done.
