// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/localizations.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../../l10n/generated/tentacle_localizations.dart';
import '../../../models/device.dart';
import '../../../models/tentacle_device_manager.dart';
import '../../../models/tentacle_timecode.dart';
import '../../../util/tentacle_util.dart';
import '../../elements/tentacle_device_icon.dart';

/// Arguments passed to the route when a [SettingsTentacleDeviceDetailsView] is pushed to the navigation stack.
class SettingsTentacleDeviceDetailsViewArguments {
  const SettingsTentacleDeviceDetailsViewArguments({required this.device});

  /// The device for which to display details.
  final TentacleDevice device;
}

/// Menu displaying detailed information about a Tentacle device.
class SettingsTentacleDeviceDetailsView extends StatelessWidget {
  const SettingsTentacleDeviceDetailsView({super.key});

  static const String route = '/timecode/tentacle/device';

  @override
  Widget build(BuildContext context) {
    final arguments = ModalRoute.of(context)?.settings.arguments as SettingsTentacleDeviceDetailsViewArguments;
    final TentacleDevice device = arguments.device;

    final tentacleManager = Provider.of<TentacleDeviceManager>(context);
    final localizations = TentacleLocalizations.of(context)!;

    return StreamBuilder(
      stream: tentacleManager.devicesStream,
      builder: (context, _) {
        final Duration timeSinceLastSeen = device.timeSinceLastSeen;
        final bool bIsTimedOut = device.bIsTimedOut;
        final Preference<String> deviceIdentifierPreference = TentacleTimecodeSource.getDeviceIdentifier(context);

        return SettingsPageScaffold(
          title: device.info.name,
          body: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 16),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    TentacleDeviceIcon(device: device, size: 61),
                    const SizedBox(width: 8),
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(getNameForTentacleProduct(context, device.info.productId)),
                        Text(
                          device.info.identifier,
                          style: Theme.of(context).textTheme.labelMedium,
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              const SettingsMenuDivider(),
              SettingsMenuItem(
                title: localizations.settingsTentacleDetailsTimecodeLabel,
                trailing: TimecodeDisplay(
                  bShowFraction: true,
                  timecodeGetter: () => device.timecode,
                ),
                trailingIconPath: null,
                bAlwaysPadForLeading: false,
              ),
              SettingsMenuItem(
                title: localizations.settingsTentacleDetailsFrameRateLabel,
                trailing: Text(device.info.frameRate.toStringAsFixed(1)),
                trailingIconPath: null,
                bAlwaysPadForLeading: false,
              ),
              const SettingsMenuDivider(),
              SettingsMenuItem(
                title: localizations.settingsTentacleDetailsLastSeenLabel,
                trailing: Text(_getTimeSinceLastSeenText(EpicCommonLocalizations.of(context)!, timeSinceLastSeen)),
                trailingIconPath: null,
                bAlwaysPadForLeading: false,
              ),
              SettingsMenuItem(
                title: localizations.settingsTentacleDetailsModeLabel,
                trailing: Text(
                  bIsTimedOut
                      ? TentacleLocalizations.of(context)!.settingsTentacleDetailsUnknown
                      : device.info.bIsInGreenMode
                          ? localizations.tentacleModeLabelGreen
                          : localizations.tentacleModeLabelRed,
                ),
                trailingIconPath: null,
                bAlwaysPadForLeading: false,
              ),
              SettingsMenuItem(
                title: localizations.settingsTentacleDetailsBatteryLabel,
                trailing: Text(bIsTimedOut ? TentacleLocalizations.of(context)!.settingsTentacleDetailsUnknown : '${device.info.batteryLevel}%'),
                trailingIconPath: null,
                bAlwaysPadForLeading: false,
              ),
              SettingsMenuItem(
                title: localizations.settingsTentacleDetailsSignalStrengthLabel,
                trailing: Text(
                  bIsTimedOut
                      ? TentacleLocalizations.of(context)!.settingsTentacleDetailsUnknown
                      : '${_getStringForSignalStrength(localizations, device.info.signalStrength)} '
                          '(${device.info.signalStrength} dBm)',
                ),
                trailingIconPath: null,
                bAlwaysPadForLeading: false,
              ),
              const SettingsMenuDivider(),
              PreferenceBuilder(
                preference: deviceIdentifierPreference,
                builder: (context, deviceIdentifier) => deviceIdentifier == device.info.identifier
                    ? SettingsMenuItem(
                        title: localizations.settingsTentacleDetailsDisconnectLabel,
                        leading: Icon(
                          Icons.link_off,
                          color: Theme.of(context).colorScheme.onPrimary.withOpacity(0.5),
                        ),
                        onTap: () => deviceIdentifierPreference.setValue(''),
                      )
                    : SettingsMenuItem(
                        title: localizations.settingsTentacleDetailsConnectLabel,
                        leading: Icon(
                          Icons.link,
                          color: Theme.of(context).colorScheme.onPrimary.withOpacity(0.5),
                        ),
                        onTap: () => deviceIdentifierPreference.setValue(device.info.identifier),
                      ),
              ),
            ],
          ),
        );
      },
    );
  }

  /// Get the localized string from the given [localizations] for a Tentacle device's [signalStrength].
  static String _getStringForSignalStrength(TentacleLocalizations localizations, int signalStrength) {
    if (signalStrength >= -60) {
      return localizations.tentacleSignalStrengthExcellent;
    } else if (signalStrength >= -70) {
      return localizations.tentacleSignalStrengthGood;
    } else if (signalStrength >= -90) {
      return localizations.tentacleSignalStrengthFair;
    }

    return localizations.tentacleSignalStrengthPoor;
  }

  /// Get the localized string from the given [localizations] indicating the device's [timeSinceLastSeen] in words.
  static String _getTimeSinceLastSeenText(EpicCommonLocalizations localizations, Duration timeSinceLastSeen) {
    if (timeSinceLastSeen.inHours > 0) {
      return localizations.relativeTimeOverAnHourAgo;
    } else if (timeSinceLastSeen.inMinutes > 0) {
      return localizations.relativeTimeMinutesAgo(timeSinceLastSeen.inMinutes);
    } else if (timeSinceLastSeen.inSeconds > 2) {
      return localizations.relativeTimeSecondsAgo(timeSinceLastSeen.inSeconds);
    }

    return localizations.relativeTimeJustNow;
  }
}
