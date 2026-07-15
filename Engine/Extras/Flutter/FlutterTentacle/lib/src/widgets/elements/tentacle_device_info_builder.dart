// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/widgets.dart';

import '../../models/device.dart';
import '../../platform/tentacle_api.g.dart';

/// A widget that rebuilds whenever a Tentacle device's info updates.
class TentacleDeviceInfoBuilder extends StatelessWidget {
  const TentacleDeviceInfoBuilder({
    super.key,
    required this.device,
    required this.builder,
  });

  /// The Tentacle device to listen to.
  final TentacleDevice device;

  /// Function that builds the widget based on the provided [deviceInfo].
  final Widget Function(TentacleDeviceInfo deviceInfo) builder;

  @override
  Widget build(BuildContext context) {
    return StreamBuilder(
      stream: device.manager.devicesStream,
      builder: (_, __) => builder(device.info),
    );
  }
}
