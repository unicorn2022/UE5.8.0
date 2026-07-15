// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../../l10n/generated/tentacle_localizations.dart';
import '../../../models/device.dart';
import '../../../models/tentacle_device_manager.dart';
import '../../../models/tentacle_timecode.dart';
import '../../elements/tentacle_signal_display.dart';
import 'settings_tentacle_device_details.dart';

/// Settings menu for picking a timecode source.
class SettingsTentacleDevicePicker extends StatelessWidget {
  const SettingsTentacleDevicePicker({super.key});

  static const String route = '/timecode/tentacle';

  @override
  Widget build(BuildContext context) {
    final tentacleManager = Provider.of<TentacleDeviceManager>(context);

    return SettingsPageScaffold(
      title: TentacleLocalizations.of(context)!.settingsTentacleSyncDeviceTitle,
      body: StreamBuilder(
        stream: tentacleManager.devicesStream,
        builder: (context, _) {
          return Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              ...tentacleManager.devices.map(
                (device) => _TentacleDeviceListItem(
                  device,
                  key: Key(device.info.identifier),
                ),
              ),
              if (tentacleManager.devices.isEmpty)
                SettingsMenuItem(
                  title: TentacleLocalizations.of(context)!.settingsTentacleSyncNoDevicesFoundLabel,
                  trailingIconPath: null,
                ),
            ],
          );
        },
      ),
    );
  }
}

/// Lists a Tentacle device.
class _TentacleDeviceListItem extends StatelessWidget {
  const _TentacleDeviceListItem(this.device, {super.key});

  /// The device to list.
  final TentacleDevice device;

  @override
  Widget build(BuildContext context) {
    final Preference<String> deviceIdentifierPreference = TentacleTimecodeSource.getDeviceIdentifier(context);

    return PreferenceBuilder(
      preference: deviceIdentifierPreference,
      builder: (context, selectedDevice) => SettingsMenuItem(
        onTap: () => deviceIdentifierPreference.setValue(device.info.identifier),
        title: device.info.name,
        leading: DefaultTextStyle(
          style: Theme.of(context).textTheme.displaySmall!,
          child: TentacleSignalDisplay(device: device),
        ),
        trailing: EpicIconButton(
          iconPath: 'packages/epic_common/assets/icons/info.svg',
          buttonSize: const Size.square(40),
          color: Theme.of(context).colorScheme.primary,
          onPressed: () => Navigator.of(context).pushNamed(
            SettingsTentacleDeviceDetailsView.route,
            arguments: SettingsTentacleDeviceDetailsViewArguments(device: device),
          ),
        ),
        trailingIconPadding: 8,
        trailingIconPath: selectedDevice == device.info.identifier ? 'packages/epic_common/assets/icons/check.svg' : null,
        bAlwaysPadForTrailingIcon: true,
      ),
    );
  }
}
