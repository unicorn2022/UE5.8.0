// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';

import '../../models/device.dart';
import '../../platform/tentacle_api.g.dart';
import 'tentacle_device_info_builder.dart';

/// Displays the signal strength of a Tentacle device.
class TentacleSignalDisplay extends StatelessWidget {
  const TentacleSignalDisplay({
    super.key,
    required this.device,
    this.size = 24,
  });

  /// The device for which to show signal strength.
  final TentacleDevice? device;

  /// The size of the icon.
  final double size;

  @override
  Widget build(BuildContext context) {
    final Color color = DefaultTextStyle.of(context).style.color ?? Theme.of(context).colorScheme.onPrimary;

    final Widget icon;

    if (device != null) {
      icon = TentacleDeviceInfoBuilder(
        device: device!,
        builder: (deviceInfo) => AssetIcon(
          path: _getSignalIconPath(deviceInfo),
        ),
      );
    } else {
      icon = AssetIcon(
        path: 'packages/epic_common/assets/icons/wifi_disconnected.svg',
        color: color,
      );
    }

    return SizedBox.square(
      dimension: size,
      child: icon,
    );
  }

  /// Given the device's info, determine the icon to display for signal level.
  String _getSignalIconPath(TentacleDeviceInfo info) {
    final signal = info.signalStrength;

    if (device?.bIsTimedOut != true) {
      if (signal >= -60) {
        return 'packages/epic_common/assets/icons/wifi_full.svg';
      } else if (signal >= -70) {
        return 'packages/epic_common/assets/icons/wifi_2_bar.svg';
      } else if (signal >= -90) {
        return 'packages/epic_common/assets/icons/wifi_1_bar.svg';
      }
    }

    return 'packages/epic_common/assets/icons/wifi_bad.svg';
  }
}
