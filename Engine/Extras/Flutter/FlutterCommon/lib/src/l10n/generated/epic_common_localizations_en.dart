// Copyright Epic Games, Inc. All Rights Reserved.
// Generated automatically by flutter_gen.


import 'epic_common_localizations.dart';

// ignore_for_file: type=lint

/// The translations for English (`en`).
class EpicCommonLocalizationsEn extends EpicCommonLocalizations {
  EpicCommonLocalizationsEn([String locale = 'en']) : super(locale);

  @override
  String get connectScreenAllConnectionsPanelEmptyButtonLabel => 'New Connection';

  @override
  String get connectScreenAllConnectionsPanelEmptyMessage => 'No Unreal Engine connections detected.';

  @override
  String get connectScreenAllConnectionsPanelTitle => 'All Unreal Engine Connections';

  @override
  String get connectScreenBeaconFailedMessage => 'Unable to search the local network for Unreal Engine instances.\n\nTry using the \"Connect manually\" option.';

  @override
  String connectScreenConnectDialogMessage(Object address) {
    return 'Connecting to $address...';
  }

  @override
  String get connectScreenConnectDialogTitle => 'Connect to Unreal Engine';

  @override
  String get connectScreenConnectionFormTitle => 'New Connection';

  @override
  String connectScreenFailedDialogMessage(Object address) {
    return 'Unable to connect to <b>$address</b>, please check the IP address and network status of the Unreal Engine instance and try again.';
  }

  @override
  String get connectScreenFailedDialogTitle => 'Failed to Connect';

  @override
  String get connectScreenManualConnectionLabel => 'Manual Connection';

  @override
  String get connectScreenMostRecentButtonLabel => 'Most Recent';

  @override
  String get connectScreenNewConnectionButtonLabel => 'Enter IP Address';

  @override
  String get connectScreenNewConnectionButtonTitle => 'New Unreal Engine Connection';

  @override
  String get connectScreenPassphraseModalTitle => 'Remote Control Passphrase';

  @override
  String get connectScreenPassphraseDisconnectErrorMessage => 'Disconnected due to invalid passphrase. Please enter the new passphrase.';

  @override
  String get connectScreenPassphraseIncorrectErrorMessage => 'Invalid passphrase. Please try again.';

  @override
  String get connectScreenQuickConnectPanelTitle => 'Quick Connect';

  @override
  String get connectScreenRootActorDialogMessage => 'The current scene contains multiple nDisplay Root Actors. Please select one to control.';

  @override
  String get connectScreenRootActorDialogTitle => 'Select nDisplay Root Actor';

  @override
  String get disconnectButtonLabel => 'Disconnect';

  @override
  String get formErrorInvalidIPAddress => 'Invalid IP address';

  @override
  String get formErrorInvalidNetworkPort => 'Invalid port';

  @override
  String logTitle(Object logDate, Object logNumber) {
    return '$logDate - Session #$logNumber';
  }

  @override
  String logTitleCurrentWrapper(Object logTitle) {
    return '$logTitle (Current)';
  }

  @override
  String get menuButtonCancel => 'Cancel';

  @override
  String get menuButtonConnect => 'Connect';

  @override
  String get menuButtonOK => 'OK';

  @override
  String get menuButtonProceed => 'Proceed';

  @override
  String get menuButtonRetry => 'Retry';

  @override
  String get menuButtonSubmit => 'Submit';

  @override
  String get modalDismissLabel => 'Dismiss';

  @override
  String get relativeTimeJustNow => 'Just now';

  @override
  String relativeTimeSecondsAgo(Object seconds) {
    return '$seconds seconds ago';
  }

  @override
  String relativeTimeMinutesAgo(Object minutes) {
    return '$minutes minutes ago';
  }

  @override
  String get relativeTimeOverAnHourAgo => 'Over an hour ago';

  @override
  String get resetButtonTooltip => 'Reset';

  @override
  String get searchBarLabel => 'Search';

  @override
  String get settingsNtpPoolLabel => 'Pool';

  @override
  String get settingsNtpPoolTitle => 'NTP Pool';

  @override
  String get settingsThirdPartyNoticeTitle => 'Third-Party Notice';

  @override
  String get settingsThirdPartyNoticesTitle => 'Third-Party Notices';

  @override
  String get settingsTimecodeTitle => 'Timecode';

  @override
  String get timecodeInvalidBadData => 'Error';

  @override
  String get timecodeInvalidNoData => 'No Data';

  @override
  String get timecodeInvalidNtpSyncFailed => 'Sync Failed';

  @override
  String get timecodeSettingsLabel => 'Timecode';

  @override
  String get timecodeSourceNtp => 'NTP';

  @override
  String get timecodeSourceSystem => 'System Time';
}
