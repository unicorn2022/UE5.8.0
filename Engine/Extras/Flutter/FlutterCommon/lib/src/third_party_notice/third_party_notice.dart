// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:convert';

import 'package:flutter/services.dart';

/// Contains data about a license notice for third-party software used by the app.
class ThirdPartyNotice {
  ThirdPartyNotice._({required this.softwareName, required this.softwareVersion, required bundle, required licensePath})
      : _bundle = bundle,
        _licensePath = licensePath;

  /// The name of the software from which this notice originates.
  final String softwareName;

  /// The version of the software used in the app.
  final String softwareVersion;

  /// The asset bundle containing the notice's data.
  final AssetBundle _bundle;

  /// The path to the asset containing the license text.
  final String _licensePath;

  /// Load the text associated with this notice's license.
  Future<String> loadLicenseText() => _bundle.loadString(_licensePath);
}

/// Provides access to third-party notice assets for the app.
/// To generate the license assets, see `{EPIC_COMMON_ROOT}/tools/generate_license_assets.py`.
class ThirdPartyNoticeManifest {
  ThirdPartyNoticeManifest({required this.assetBundle, this.licenseDirectory = 'assets/licenses'});

  /// The asset bundle from which to load assets.
  final AssetBundle assetBundle;

  /// The root of the directory containing license assets.
  final String licenseDirectory;

  /// Get a list of all the third party notices packaged with the app.
  Future<List<ThirdPartyNotice>> listAll() async {
    final manifestText = await assetBundle.loadString('$licenseDirectory/manifest.json');
    final manifest = jsonDecode(manifestText) as Map<String, dynamic>;

    return manifest.entries
        .map(
          (entry) => ThirdPartyNotice._(
            softwareName: entry.key,
            softwareVersion: entry.value['version'],
            licensePath: '$licenseDirectory/text/${entry.value['file']}',
            bundle: assetBundle,
          ),
        )
        .toList(growable: false);
  }
}
