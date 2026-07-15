import 'dart:io';

import 'package:flutter/services.dart';
import 'package:package_info_plus/package_info_plus.dart';

/// Get a user-friendly version number string for the app package.
Future<String> getFriendlyPackageVersion() async {
  final packageInfo = await PackageInfo.fromPlatform();

  String versionString = 'v${packageInfo.version}';

  if (appFlavor != null) {
    versionString += '-$appFlavor';
  }

  if (packageInfo.buildNumber.isNotEmpty) {
    versionString += ' (${packageInfo.buildNumber})';
  }

  return versionString;
}

/// Make a string containing verbose information about the app package version.
Future<String> getVerbosePackageVersion() async {
  final packageInfo = await PackageInfo.fromPlatform();
  final platformName = Platform.operatingSystem;
  final String friendlyVersion = await getFriendlyPackageVersion();

  return '${packageInfo.appName} - $friendlyVersion / $platformName\n'
      'Package: ${packageInfo.packageName}\n'
      'Store: ${packageInfo.installerStore ?? 'N/A'}\n'
      'Signature: ${Platform.isAndroid ? packageInfo.buildSignature : 'N/A'}';
}
