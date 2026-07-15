// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:epic_common/preferences.dart';
import 'package:epic_common/timecode.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../l10n/generated/tentacle_localizations.dart';
import '../widgets/elements/tentacle_signal_display.dart';
import '../widgets/screens/settings/settings_tentacle_device_details.dart';
import '../widgets/screens/settings/settings_tentacle_device_picker.dart';
import 'device.dart';
import 'tentacle_device_manager.dart';

const String _timecodeTypeName = 'tentacle';

/// Provides timecodes synchronized with a Tentacle device.
class TentacleTimecodeSource extends TimecodeSource {
  @override
  String getDisplayName(BuildContext context) => TentacleLocalizations.of(context)!.timecodeSourceTentacle;

  @override
  String get iconPath => 'packages/flutter_tentacle/assets/icons/timecode_tentacle.svg';

  @override
  String get internalName => _timecodeTypeName;

  @override
  Widget makeSettingsEntry(bool bIsSelected) => _TentacleTimecodeSettingsItem(bIsSelected);

  @override
  TimecodeSynchronizer makeSynchronizer(BuildContext context) => _TentacleTimecodeSynchronizer(context);

  @override
  Widget? Function(String route)? get getSettingsPage => (String route) {
        switch (route) {
          case SettingsTentacleDevicePicker.route:
            return const SettingsTentacleDevicePicker();

          case SettingsTentacleDeviceDetailsView.route:
            return const SettingsTentacleDeviceDetailsView();
        }

        return null;
      };

  /// Given a build [context], get the preference identifying the Tentacle device to use.
  static Preference<String> getDeviceIdentifier(BuildContext context) => context.read<PreferencesBundle>().persistent.getString(
        'timecode.tentacle.identifier',
        defaultValue: '',
      );
}

class _TentacleTimecodeSynchronizer implements TimecodeSynchronizer {
  _TentacleTimecodeSynchronizer(
    BuildContext context,
  )   : _deviceIdentifier = TentacleTimecodeSource.getDeviceIdentifier(context),
        _deviceManager = context.read<TentacleDeviceManager>() {
    _tentacleSubscription = _deviceManager.devicesStream.listen((_) {});
  }

  /// The manager for accessing Tentacle devices.
  final TentacleDeviceManager _deviceManager;

  /// The unique identifier of the device from which to retrieve the timecode.
  final Preference<String> _deviceIdentifier;

  /// Subscription to the TentacleDeviceManager's device stream. We don't care about the values, but maintain the
  /// subscription so that the manager will continue to scan and remain in sync with the device.
  late final StreamSubscription _tentacleSubscription;

  @override
  Timecode get timecode {
    final TentacleDevice? device = _deviceManager.getDeviceByIdentifier(_deviceIdentifier.getValue());
    if (device == null) {
      return Timecode.invalid(
        TimecodeInvalidReason.sourceSpecific,
        getSourceSpecificInvalidMessage: (context) => TentacleLocalizations.of(context)!.timecodeInvalidNoTentacleDevice,
      );
    }

    return device.timecode;
  }

  @override
  void dispose() {
    _tentacleSubscription.cancel();
  }
}

/// Displays the Tentacle timecode and settings in the settings menu.
class _TentacleTimecodeSettingsItem extends TimecodeSettingsItem {
  const _TentacleTimecodeSettingsItem(super.bIsSelected);

  @override
  Widget build(BuildContext context) {
    final timecodeManager = Provider.of<TimecodeManager>(context);

    final header = SettingsMenuItem(
      title: TentacleLocalizations.of(context)!.timecodeSourceTentacle,
      onTap: () => timecodeManager.sourceName.setValue(_timecodeTypeName),
      iconPath: 'packages/flutter_tentacle/assets/icons/timecode_tentacle.svg',
      trailingIconPath: trailingIconPath,
      trailing: buildTimecodeDisplay(),
      bAlwaysPadForTrailingIcon: true,
    );

    if (!bIsSelected) {
      return header;
    }

    final tentacleManager = Provider.of<TentacleDeviceManager>(context);

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        header,
        SettingsMenuItem(
          title: TentacleLocalizations.of(context)!.settingsTentacleSyncDeviceLabel,
          onTap: () => Navigator.of(context).pushNamed(SettingsTentacleDevicePicker.route),
          trailing: PreferenceBuilder(
            preference: TentacleTimecodeSource.getDeviceIdentifier(context),
            builder: (context, deviceIdentifier) => FutureBuilder(
              future: tentacleManager.getFutureDeviceByIdentifier(deviceIdentifier),
              // Pass initial data so the first frame drawn already has the device if it's already been detected
              initialData: tentacleManager.getDeviceByIdentifier(deviceIdentifier),
              builder: (context, deviceSnapshot) {
                final TentacleDevice? device = deviceSnapshot.data;
                final String deviceName = device?.info.name ?? TentacleLocalizations.of(context)!.settingsTentacleSyncNoDeviceName;

                return Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Flexible(
                      child: ConstrainedBox(
                        constraints: const BoxConstraints(maxWidth: 200),
                        child: Text(
                          deviceName,
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                    ),
                    const SizedBox(width: 8),
                    DefaultTextStyle(
                      style: Theme.of(context).textTheme.displaySmall!,
                      child: TentacleSignalDisplay(device: device),
                    ),
                  ],
                );
              },
            ),
          ),
        ),
      ],
    );
  }
}
