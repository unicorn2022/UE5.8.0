#!/usr/bin/env bash

# Determine which version of the Unreal Engine we will be building container images for
UNREAL_ENGINE_VERSION="$1"
if [[ -z "$UNREAL_ENGINE_VERSION" ]]; then
	echo 'Usage:'
	echo 'build.sh VERSION [REPOSITORY] [CHANGELIST] [TAG_SUFFIX]'
	echo
	echo 'Please specify an Unreal Engine version to build container images for.'
	echo 'This can be a release number like `5.0.0`, or a Git branch/tag/commit.'
	exit 1
fi

# Determine which Git branch/tag/commit we will be pulling the Unreal Engine source code from
# (If the specified version value contains a dash or does not contain two dots then we treat it as a branch/tag/commit,
#  otherwise we treat it as a release number and append `-release` to get the corresponding Git release tag)
GIT_REF="${UNREAL_ENGINE_VERSION}-release"
dots=$(echo "${UNREAL_ENGINE_VERSION}" | grep -o '[.]' | wc -l)
if [[ "${UNREAL_ENGINE_VERSION}" == *"-"* ]] || [[ "${dots}" != "2" ]]; then
	GIT_REF="${UNREAL_ENGINE_VERSION}"
fi

# If the user specified a custom Git repository to clone from then use that
GIT_REPO=https://github.com/EpicGames/UnrealEngine.git
if [[ ! -z "$2" ]]; then
	GIT_REPO="$2"
fi

# Determine whether the user specified a changelist number to set in Build.version
CHANGELIST_OVERRIDE=""
if [[ ! -z "$3" ]]; then
	CHANGELIST_OVERRIDE="$3"
fi

# Determine whether the user specified a custom tag suffix to use instead of the Unreal Engine version number
TAG_SUFFIX="$UNREAL_ENGINE_VERSION"
if [[ ! -z "$4" ]]; then
	TAG_SUFFIX="$4"
fi

# If a custom log directory was specified then use it
LOG_DIRECTORY="./logs"
if [[ ! -z "$CONTAINER_BUILD_LOGS" ]]; then
	LOG_DIRECTORY="$CONTAINER_BUILD_LOGS"
fi

# Verify that the user has placed their GitHub username in the file `username.txt`
if [ ! -f ./username.txt ]; then
	echo 'Error: required credentials file missing!'
	echo
	echo 'Please place your GitHub username in a text file called `username.txt` in the current directory.'
	exit 1
fi

# Verify that the user has placed their GitHub personal access token in the file `password.txt`
if [ ! -f ./password.txt ]; then
	echo 'Error: required credentials file missing!'
	echo
	echo 'Please place your GitHub personal access token in a text file called `password.txt` in the current directory.'
	echo
	echo 'Note that you should NOT use your GitHub password. See the following page for details on why a token is needed:'
	echo 'https://github.blog/2020-12-15-token-authentication-requirements-for-git-operations/'
	exit 1
fi

# Assemble the common arguments to pass to the `docker buildx build` command
commonArgs=(
	--build-arg 'BASEIMAGE=nvidia/opengl:1.2-glvnd-devel-ubuntu22.04'
	--build-arg "GIT_REPO=${GIT_REPO}"
	--build-arg "GIT_BRANCH=${GIT_REF}"
	--build-arg "CHANGELIST=${CHANGELIST_OVERRIDE}"
	--build-arg "BUILDGRAPH_ARGS=${CONTAINER_BUILDGRAPH_ARGS}"
	--secret 'id=username,src=username.txt'
	--secret 'id=password,src=password.txt'
	--secret 'id=IsBuildMachine,env=IsBuildMachine'
	--secret 'id=UE_HORDE_URL,env=UE_HORDE_URL'
	--secret 'id=UE_HORDE_TOKEN,env=UE_HORDE_TOKEN'
	--secret 'id=UE_CloudDataCacheAccessToken,env=UE_CloudDataCacheAccessToken'
	--secret 'id=UE_CloudDataCacheHost,env=UE_CloudDataCacheHost'
	--secret 'id=UE_CloudDataCacheHttpVersion,env=UE_CloudDataCacheHttpVersion'
	--secret 'id=UE_CloudDataCacheWriteOnly,env=UE_CloudDataCacheWriteOnly'
	--secret 'id=UE_CloudPublishDescriptorHost,env=UE_CloudPublishDescriptorHost'
	--secret 'id=UE_CloudPublishDescriptorHttpVersion,env=UE_CloudPublishDescriptorHttpVersion'
	--secret 'id=UE_CloudPublishHost,env=UE_CloudPublishHost'
	--secret 'id=UE_CloudPublishHttpVersion,env=UE_CloudPublishHttpVersion'
	--platform linux/amd64
	--progress=plain
)

# Attempts to build a container image, and prints the log output if it fails
function buildImage() {
	local image="$1"
	local context="$2"
	local logfile="$3"
	local args=("${@:4}")
	echo "[docker buildx build -t $image" "${args[@]}" "$context]"
	echo
	docker buildx build -t "$image" "${args[@]}" "$context" &> "$LOG_DIRECTORY/$logfile" || {
		echo 'Error: image build failed! Log output:'
		echo
		cat "$LOG_DIRECTORY/$logfile"
		exit 1
	}
}

# Print our build configuration values
echo 'Build configuration'
echo '-------------------'
echo
echo "Unreal Engine Version: ${UNREAL_ENGINE_VERSION}"
echo "Git Repository:        ${GIT_REPO}"
echo "Git Branch/Tag/Commit: ${GIT_REF}"
echo "Changelist Override:   ${CHANGELIST_OVERRIDE:-(None)}"
echo "Image Tag Suffix:      ${TAG_SUFFIX}"
echo

# Halt immediately if any command fails
set -e

# Ensure the log directory exists and clean up any leftover logs from previous builds
test -d "$LOG_DIRECTORY" || mkdir -p "$LOG_DIRECTORY"
rm -f "$LOG_DIRECTORY"/*.log

# Build a runtime container image suitable for projects that do not use Pixel Streaming
echo 'Building the runtime image...'
buildImage \
	'ghcr.io/epicgames/unreal-engine:runtime' \
	./runtime \
	runtime.log

# Build a runtime container image suitable for projects that use Pixel Streaming
echo 'Building the Pixel Streaming runtime image...'
buildImage \
	'ghcr.io/epicgames/unreal-engine:runtime-pixel-streaming' \
	./runtime-pixel-streaming \
	runtime-pixel-streaming.log

# Build a container image that encapsulates a full Installed Build of the Unreal Engine
echo 'Building the full `unreal-engine` image for Unreal Engine version' "${UNREAL_ENGINE_VERSION}..."
buildImage \
	"ghcr.io/epicgames/unreal-engine:dev-${TAG_SUFFIX}" \
	./dev \
	dev-full.log \
	"${commonArgs[@]}"

# Build a "slim" version of the container image that excludes debug symbols and templates
echo 'Building the slim `unreal-engine` image for Unreal Engine version' "${UNREAL_ENGINE_VERSION}..."
buildImage \
	"ghcr.io/epicgames/unreal-engine:dev-slim-${TAG_SUFFIX}" \
	./dev-slim \
	dev-slim.log \
	"${commonArgs[@]}"

# Build the container image for the Multi-User Editing server, which pulls files from our other images
echo 'Building the `multi-user-server` image for Unreal Engine version' "${UNREAL_ENGINE_VERSION}..."
buildImage \
	"ghcr.io/epicgames/multi-user-server:${TAG_SUFFIX}" \
	./multi-user-server \
	multi-user-server.log \
	--build-arg "UNREAL_ENGINE_VERSION=${TAG_SUFFIX}"
