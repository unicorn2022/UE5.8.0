// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/timecode.dart';

import '../platform/tentacle_api.dart';
import '../platform/tentacle_api.g.dart';
import 'tentacle_device_manager.dart';

/// A handle with convenience functions for a Tentacle device detected by the Tentacle BLE API.
class TentacleDevice {
  TentacleDevice({
    required this.nativeIndex,
    required this.manager,
    TentacleApi? api,
  }) : api = api ?? TentacleApi.instance;

  /// How many seconds to go without seeing a device before considering it missing.
  static const _lastSeenTimeout = Duration(seconds: 10);

  /// The index of the device in the native Tentacle library.
  final int nativeIndex;

  /// The device manager that created this.
  final TentacleDeviceManager manager;

  /// The API used to communicate with the host language.
  final TentacleApi api;

  /// The cached information associated with this device.
  TentacleDeviceInfo get info => manager.getDeviceInfo(nativeIndex);

  /// The device's current timecode.
  Timecode get timecode => Timecode.fromSeconds(api.getDeviceTimecodeSeconds(this));

  /// How long it's been since the device was last seen.
  Duration get timeSinceLastSeen {
    final secondsSinceLastSeen = api.timestamp - info.lastSeenTimestamp;
    return Duration(microseconds: (secondsSinceLastSeen * Duration.microsecondsPerSecond).toInt());
  }

  /// Whether this device was last seen long enough ago that it's considered missing.
  bool get bIsTimedOut => timeSinceLastSeen > _lastSeenTimeout;
}
