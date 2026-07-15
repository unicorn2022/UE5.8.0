// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/preferences.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../util/net_utilities.dart';
import '../../util/streaming_preferences_extensions.dart';

/// Holds user settings that impact the state of the live link vcam app.
class VCamSettings {
  VCamSettings(PreferencesBundle preferences)
      : bShowInfoBar = preferences.persistent.getBool('common.showInfoBar', defaultValue: false),
        bDeveloperModeEnabled = preferences.persistent.getBool('common.enableDeveloperMode', defaultValue: false),
        bShowPixelStreamingStats =
            preferences.persistent.getBool('common.showPixelStreamingStats', defaultValue: false),
        maxArUpdatesPerSecond = preferences.persistent.getInt('common.maxArUpdateRate', defaultValue: 0),
        arUpdateSkipChance = preferences.persistent.getInt('common.arUpdateSkipChance', defaultValue: 0),
        lastConnection = preferences.persistent.getEngineConnectionData('common.lastConnection') {
    bDeveloperModeEnabled.listen(_onDeveloperModeChanged);
  }

  /// Whether or not to show the info bar.
  final Preference<bool> bShowInfoBar;

  /// Whether or not developer mode is enabled.
  final Preference<bool> bDeveloperModeEnabled;

  /// Whether or not to show pixel streaming stats.
  final Preference<bool> bShowPixelStreamingStats;

  /// The maximum rate at which to send AR updates to the engine in frames per second.
  final Preference<int> maxArUpdatesPerSecond;

  /// The % chance of skipping each AR update chance.
  final Preference<int> arUpdateSkipChance;

  /// Connection data for the last successful engine connection.
  final Preference<EngineConnectionData> lastConnection;

  /// Called when [bDeveloperModeEnabled] changes.
  void _onDeveloperModeChanged(bool bEnabled) {
    if (bEnabled) {
      return;
    }

    bShowPixelStreamingStats.setValue(bShowPixelStreamingStats.defaultValue);
    maxArUpdatesPerSecond.setValue(maxArUpdatesPerSecond.defaultValue);
    arUpdateSkipChance.setValue(arUpdateSkipChance.defaultValue);
  }
}
