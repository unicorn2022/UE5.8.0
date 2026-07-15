// Copyright Epic Games, Inc. All Rights Reserved.

import 'platform/tentacle_api.dart';

/// Holds static functions for the Tentacle plugin.
class TentaclePlugin {
  /// Shut down the plugin, freeing any held resources.
  static void shutdown() {
    TentacleApi.instance.dispose();
  }
}
