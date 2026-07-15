// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/widgets.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../../localizations.dart';
import '../../../timecode.dart';
import '../../timecode/ntp_timecode.dart';
import '../modal.dart';
import '../settings.dart';
import 'timecode_settings.dart';

/// Displays the NTP timecode and settings in the settings menu.
class NtpTimecodeSettingsItem extends TimecodeSettingsItem {
  const NtpTimecodeSettingsItem(super.bIsSelected);

  @override
  Widget build(BuildContext context) {
    final timecodeManager = Provider.of<TimecodeManager>(context);

    final header = SettingsMenuItem(
      title: EpicCommonLocalizations.of(context)!.timecodeSourceNtp,
      onTap: () => timecodeManager.sourceName.setValue(NtpTimecodeSource.staticInternalName),
      iconPath: NtpTimecodeSource.staticIconPath,
      trailingIconPath: trailingIconPath,
      trailing: buildTimecodeDisplay(),
      bAlwaysPadForTrailingIcon: true,
    );

    if (!bIsSelected) {
      return header;
    }

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        header,
        SettingsMenuItem(
          title: EpicCommonLocalizations.of(context)!.settingsNtpPoolLabel,
          onTap: () => _changeNtpServer(context),
          trailing: PreferenceBuilder(
            preference: NtpTimecodeSource.getServerAddress(context),
            builder: (_, ntpServerAddress) => Text(ntpServerAddress),
          ),
        ),
      ],
    );
  }

  /// Prompt the user to change the NTP server.
  void _changeNtpServer(BuildContext context) {
    final serverAddress = NtpTimecodeSource.getServerAddress(context);

    GenericModalDialogRoute.showDialog(
      context: context,
      builder: (context) => StringTextInputModalDialog(
        title: EpicCommonLocalizations.of(context)!.settingsNtpPoolTitle,
        initialValue: serverAddress.getValue(),
        hintText: serverAddress.defaultValue,
        handleResult: (TextInputModalDialogResult<String> result) {
          if (result.action == TextInputModalDialogAction.apply) {
            if (result.value!.isEmpty) {
              // Reset to default if nothing provided
              serverAddress.setValue(serverAddress.defaultValue);
            } else {
              // Update to new value
              serverAddress.setValue(result.value!);
            }
          }

          // Complete immediately
          return Future.value();
        },
      ),
    );
  }
}
