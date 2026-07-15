#!/bin/bash

# Blog post for setting up arm multiarch docker images:
#   Cross Building and Running Multi-Arch Docker Images
#   https://www.ecliptik.com/Cross-Building-and-Running-Multi-Arch-Docker-Images/
#
#   this link might be more relevant for modern docker: https://www.docker.com/blog/getting-started-with-docker-for-arm-on-linux
# TL;DR:
#   apt-get install qemu-user-static
#   docker run --rm --privileged multiarch/qemu-user-static:register
#
# To test docker images, run something like this:
#   docker run -v /epic:/epic -it --platform linux/arm64 rockylinux/rockylinux:8.4 /bin/bash

SCRIPT_DIR=$(cd "$(dirname "$BASH_SOURCE")" ; pwd)
VKLAYER_DIR=${SCRIPT_DIR}/../1.4.341

DISTRO=${1:-Rocky8}

TOOLCHAIN=${UE_SDKS_ROOT}/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8

if [ -d "${VKLAYER_DIR}/.git" ] && git -C ${VKLAYER_DIR} diff --quiet && git -C ${VKLAYER_DIR} diff --cached --quiet; then
	echo "Repository already present and unmodified, skippping clone"
else
	rm -rf ${VKLAYER_DIR}
	git clone -b vulkan-sdk-1.4.341 https://github.com/KhronosGroup/Vulkan-ValidationLayers ${VKLAYER_DIR}
fi

MakeToolchainCmake()
{
	local ARCH=$1

	cat > "${SCRIPT_DIR}/toolchain.cmake" << EOF
set(CMAKE_C_COMPILER clang CACHE PATH "" FORCE)
set(CMAKE_CXX_COMPILER clang++ CACHE PATH "" FORCE)
set(CMAKE_CXX_FLAGS "-nostdinc++ -I/multiarch/${ARCH}/include/c++/v1" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=lld -nodefaultlibs /multiarch/${ARCH}/lib64/libc++.a /multiarch/${ARCH}/lib64/libc++abi.a -lm -lc -lgcc_s" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS "-fuse-ld=lld -nodefaultlibs /multiarch/${ARCH}/lib64/libc++.a /multiarch/${ARCH}/lib64/libc++abi.a -lm -lc -lgcc_s" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld -nodefaultlibs /multiarch/${ARCH}/lib64/libc++.a /multiarch/${ARCH}/lib64/libc++abi.a -lm -lc -lgcc_s" CACHE STRING "" FORCE)
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "" CACHE STRING "" FORCE)
set(CMAKE_C_IMPLICIT_LINK_LIBRARIES   "" CACHE STRING "" FORCE)
set(CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES "" CACHE STRING "" FORCE)
set(CMAKE_C_IMPLICIT_LINK_DIRECTORIES   "" CACHE STRING "" FORCE)
EOF

}

BuildWithDocker()
{
	local Arch=$1
	local Platform=$2
	local Image=$3
	local ImageName=temp_build_linux_vklayers
	local DestDir=${SCRIPT_DIR}/../../../../Binaries/ThirdParty/Vulkan/Linux

	echo Building ${Arch}...
	MakeToolchainCmake ${Arch}

	# you can add ';bash' at the end of the bash -c inside the double quotes to spawn an interactive shell after the build is done.
	# this can be helpful for iteration of build errors so that you don't have to wait for the container to be recreated each time
	echo sudo docker run -it --name ${ImageName} --platform ${Platform} -v ${SCRIPT_DIR}/../../Vulkan:/Vulkan -v ${VKLAYER_DIR}:/VkLayer -v ${SCRIPT_DIR}:/src -v ${TOOLCHAIN}:/multiarch ${Image} bash -c "/src/docker-build-vklayers.sh ${DISTRO}"
	sudo docker run -it --name ${ImageName} --platform ${Platform} -v ${SCRIPT_DIR}/../../Vulkan:/Vulkan -v ${VKLAYER_DIR}:/VkLayer -v ${SCRIPT_DIR}:/src -v ${TOOLCHAIN}:/multiarch ${Image} bash -c "/src/docker-build-vklayers.sh ${DISTRO}"
	
	echo Copying files...
	rm -f ${DestDir}/libVkLayer_khronos_validation.so ${DestDir}/VkLayer_khronos_validation.json

	sudo docker cp ${ImageName}:/build/layers/libVkLayer_khronos_validation.so ${DestDir}
	sudo docker cp ${ImageName}:/build/layers/VkLayer_khronos_validation.json ${DestDir}

	echo Cleaning up...
	sudo docker rm ${ImageName}
}

sudo docker run --rm --privileged docker/binfmt:820fdd95a9972a5308930a2bdfb8573dd4447ad3


if [ ${DISTRO} == "Rocky8" ]; then
	echo Building SDL on Rocky 8.4
	BuildWithDocker x86_64-unknown-linux-gnu      linux/amd64    rockylinux/rockylinux:8.4
# leaving this in place in case we ever decide to support arm layers as well
#	BuildWithDocker aarch64-unknown-linux-gnueabi linux/arm64    rockylinux/rockylinux:8.4
else
	echo Unsupported distro: ${DISTRO}
	exit 1
fi
