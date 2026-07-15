#!/bin/bash
# Creates an embedded framework zip from libmetalirconverter.dylib for iOS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_NAME="libmetalirconverter"
DYLIB_NAME="libmetalirconverter.dylib"
BINARIES_DIR="${SCRIPT_DIR}/../../../../Binaries/ThirdParty/Apple/MetalShaderConverter/IOS"
DYLIB_PATH="${BINARIES_DIR}/${DYLIB_NAME}"
OUTPUT_DIR="${BINARIES_DIR}"

if [ ! -f "$DYLIB_PATH" ]; then
    echo "Error: dylib not found at $DYLIB_PATH"
    exit 1
fi

cd "$OUTPUT_DIR"

# Build the .framework bundle structure
FRAMEWORK_DIR="${FRAMEWORK_NAME}.framework"
rm -rf "$FRAMEWORK_DIR"
mkdir -p "$FRAMEWORK_DIR"

# Copy the dylib as the framework binary (no extension - Apple framework convention)
cp "$DYLIB_PATH" "${FRAMEWORK_DIR}/${FRAMEWORK_NAME}"

# Fix the install name to match the framework path
# Without this, the dynamic linker won't find the library at runtime
install_name_tool -id "@rpath/${FRAMEWORK_DIR}/${FRAMEWORK_NAME}" "${FRAMEWORK_DIR}/${FRAMEWORK_NAME}"

# Generate Info.plist
cat > "${FRAMEWORK_DIR}/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>${FRAMEWORK_NAME}</string>
	<key>CFBundleIdentifier</key>
	<string>${FRAMEWORK_NAME}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundlePackageType</key>
	<string>FMWK</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleSupportedPlatforms</key>
	<array>
		<string>iPhoneOS</string>
	</array>
	<key>CFBundleVersion</key>
	<string>1.0</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
	<key>MinimumOSVersion</key>
	<string>15.0</string>
	<key>DTPlatformName</key>
	<string>iphoneos</string>
	<key>DTPlatformVersion</key>
	<string>26.4</string>
	<key>DTSDKName</key>
	<string>iphoneos26.4</string>
	<key>UIRequiredDeviceCapabilities</key>
	<array>
		<string>arm64</string>
	</array>
</dict>
</plist>
PLIST

# Package as .embeddedframework.zip (UBT expects this structure)
EMBEDDED_DIR="${FRAMEWORK_NAME}.embeddedframework"
rm -rf "$EMBEDDED_DIR"
mkdir -p "$EMBEDDED_DIR"
cp -R "$FRAMEWORK_DIR" "$EMBEDDED_DIR/"

ZIP_NAME="${FRAMEWORK_NAME}.embeddedframework.zip"
rm -f "$ZIP_NAME"
zip -r "$ZIP_NAME" "$EMBEDDED_DIR"

# Clean up intermediate directories
rm -rf "$EMBEDDED_DIR" "$FRAMEWORK_DIR"

echo "Created ${ZIP_NAME} successfully"
