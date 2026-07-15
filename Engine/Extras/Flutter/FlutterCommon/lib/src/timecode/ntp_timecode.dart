// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:logging/logging.dart';
import 'package:ntp/ntp.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../../localizations.dart';
import '../../../timecode.dart';
import '../../preferences.dart';
import '../widgets/modal.dart';
import '../widgets/settings.dart';
import '../widgets/timecode/timecode_settings.dart';

final _log = Logger('NtpTimecodeSynchronizer');

/// Provides timecodes synchronized with an NTP server.
class NtpTimecodeSource extends TimecodeSource {
  static const String staticInternalName = 'ntp';
  static const String staticIconPath = 'packages/epic_common/assets/icons/timecode_ntp.svg';

  @override
  String getDisplayName(BuildContext context) => EpicCommonLocalizations.of(context)!.timecodeSourceNtp;

  @override
  String get iconPath => staticIconPath;

  @override
  String get internalName => staticInternalName;

  @override
  Widget makeSettingsEntry(bool bIsSelected) => _NtpTimecodeSettingsItem(bIsSelected);

  @override
  TimecodeSynchronizer makeSynchronizer(BuildContext context) => _NtpTimecodeSynchronizer(context);

  /// Given a build [context], get the preference identifying the NTP server to use.
  static Preference<String> getServerAddress(BuildContext context) =>
      context.read<PreferencesBundle>().persistent.getString(
            'timecode.ntp.serverAddress',
            defaultValue: 'time.apple.com',
          );
}

class _NtpTimecodeSynchronizer implements TimecodeSynchronizer {
  _NtpTimecodeSynchronizer(BuildContext context) : _serverAddress = NtpTimecodeSource.getServerAddress(context) {
    _serverAddressSubscription = _serverAddress.listen((_) => _syncOffset());
    _syncOffset();
  }

  /// How often to sync time with the NTP server.
  static final Duration _syncInterval = Duration(minutes: 1);

  /// The address of the NTP server to sync with.
  final Preference<String> _serverAddress;

  /// Subscription to the server address preference.
  late final StreamSubscription _serverAddressSubscription;

  /// The last cached offset from the server time. If null, no offset could be retrieved.
  /// Starts at zero so that we initially display the system time rather than flickering an invalid timecode until the
  /// first server response.
  Duration? _lastOffset = Duration.zero;

  /// Synchronizes the time with the NTP server when elapsed.
  Timer? _syncTimer;

  /// Whether this has been disposed.
  bool _bIsDisposed = false;

  @override
  Timecode get timecode => _lastOffset != null
      ? Timecode.fromDateTime(DateTime.now().add(_lastOffset!))
      : Timecode.invalid(
          TimecodeInvalidReason.sourceSpecific,
          getSourceSpecificInvalidMessage: (context) =>
              EpicCommonLocalizations.of(context)!.timecodeInvalidNtpSyncFailed,
        );

  @override
  void dispose() {
    _syncTimer?.cancel();
    _serverAddressSubscription.cancel();
    _bIsDisposed = true;
  }

  /// Request the latest NTP time offset. When complete, store it and set the timer to sync again after an interval.
  void _syncOffset() async {
    if (_bIsDisposed) {
      return;
    }

    int? offsetMs;
    try {
      offsetMs = await NTP.getNtpOffset(lookUpAddress: _serverAddress.getValue());
    } catch (e) {
      offsetMs = null;

      _log.warning('Failed to sync with NTP server "$_serverAddress"', e);
    }

    if (_bIsDisposed) {
      return;
    }

    _lastOffset = offsetMs != null ? Duration(milliseconds: offsetMs) : null;
    _syncTimer = Timer(_syncInterval, _syncOffset);
  }
}

/// Displays the NTP timecode and settings in the settings menu.
class _NtpTimecodeSettingsItem extends TimecodeSettingsItem {
  const _NtpTimecodeSettingsItem(super.bIsSelected);

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
