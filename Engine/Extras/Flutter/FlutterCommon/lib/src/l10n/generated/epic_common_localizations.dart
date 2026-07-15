// Copyright Epic Games, Inc. All Rights Reserved.
// Generated automatically by flutter_gen.

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart' as intl;

import 'epic_common_localizations_en.dart';

// ignore_for_file: type=lint

/// Callers can lookup localized strings with an instance of EpicCommonLocalizations
/// returned by `EpicCommonLocalizations.of(context)`.
///
/// Applications need to include `EpicCommonLocalizations.delegate()` in their app's
/// `localizationDelegates` list, and the locales they support in the app's
/// `supportedLocales` list. For example:
///
/// ```dart
/// import 'generated/epic_common_localizations.dart';
///
/// return MaterialApp(
///   localizationsDelegates: EpicCommonLocalizations.localizationsDelegates,
///   supportedLocales: EpicCommonLocalizations.supportedLocales,
///   home: MyApplicationHome(),
/// );
/// ```
///
/// ## Update pubspec.yaml
///
/// Please make sure to update your pubspec.yaml to include the following
/// packages:
///
/// ```yaml
/// dependencies:
///   # Internationalization support.
///   flutter_localizations:
///     sdk: flutter
///   intl: any # Use the pinned version from flutter_localizations
///
///   # Rest of dependencies
/// ```
///
/// ## iOS Applications
///
/// iOS applications define key application metadata, including supported
/// locales, in an Info.plist file that is built into the application bundle.
/// To configure the locales supported by your app, you’ll need to edit this
/// file.
///
/// First, open your project’s ios/Runner.xcworkspace Xcode workspace file.
/// Then, in the Project Navigator, open the Info.plist file under the Runner
/// project’s Runner folder.
///
/// Next, select the Information Property List item, select Add Item from the
/// Editor menu, then select Localizations from the pop-up menu.
///
/// Select and expand the newly-created Localizations item then, for each
/// locale your application supports, add a new item and select the locale
/// you wish to add from the pop-up menu in the Value field. This list should
/// be consistent with the languages listed in the EpicCommonLocalizations.supportedLocales
/// property.
abstract class EpicCommonLocalizations {
  EpicCommonLocalizations(String locale) : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static EpicCommonLocalizations? of(BuildContext context) {
    return Localizations.of<EpicCommonLocalizations>(context, EpicCommonLocalizations);
  }

  static const LocalizationsDelegate<EpicCommonLocalizations> delegate = _EpicCommonLocalizationsDelegate();

  /// A list of this localizations delegate along with the default localizations
  /// delegates.
  ///
  /// Returns a list of localizations delegates containing this delegate along with
  /// GlobalMaterialLocalizations.delegate, GlobalCupertinoLocalizations.delegate,
  /// and GlobalWidgetsLocalizations.delegate.
  ///
  /// Additional delegates can be added by appending to this list in
  /// MaterialApp. This list does not have to be used at all if a custom list
  /// of delegates is preferred or required.
  static const List<LocalizationsDelegate<dynamic>> localizationsDelegates = <LocalizationsDelegate<dynamic>>[
    delegate,
    GlobalMaterialLocalizations.delegate,
    GlobalCupertinoLocalizations.delegate,
    GlobalWidgetsLocalizations.delegate,
  ];

  /// A list of this localizations delegate's supported locales.
  static const List<Locale> supportedLocales = <Locale>[
    Locale('en')
  ];

  /// Label of the button shown when the connect screen has no connections to display
  ///
  /// In en, this message translates to:
  /// **'New Connection'**
  String get connectScreenAllConnectionsPanelEmptyButtonLabel;

  /// Message shown when no engine instances are detected
  ///
  /// In en, this message translates to:
  /// **'No Unreal Engine connections detected.'**
  String get connectScreenAllConnectionsPanelEmptyMessage;

  /// Title for the panel showing all detected engine instances in the connection screen
  ///
  /// In en, this message translates to:
  /// **'All Unreal Engine Connections'**
  String get connectScreenAllConnectionsPanelTitle;

  /// Message shown when the connect screen beacon fails to search the network for Unreal Engine instances
  ///
  /// In en, this message translates to:
  /// **'Unable to search the local network for Unreal Engine instances.\n\nTry using the \"Connect manually\" option.'**
  String get connectScreenBeaconFailedMessage;

  /// Message in the spinner dialog shown while waiting for the engine to connect at the given address
  ///
  /// In en, this message translates to:
  /// **'Connecting to {address}...'**
  String connectScreenConnectDialogMessage(Object address);

  /// Title of the spinner dialog shown while waiting for the engine to connect
  ///
  /// In en, this message translates to:
  /// **'Connect to Unreal Engine'**
  String get connectScreenConnectDialogTitle;

  /// Title of the form shown when connecting to a new engine by manually entering its information
  ///
  /// In en, this message translates to:
  /// **'New Connection'**
  String get connectScreenConnectionFormTitle;

  /// Message body of the dialog shown when the connection to the given address fails to complete
  ///
  /// In en, this message translates to:
  /// **'Unable to connect to <b>{address}</b>, please check the IP address and network status of the Unreal Engine instance and try again.'**
  String connectScreenFailedDialogMessage(Object address);

  /// Title of the dialog shown when the connection fails to complete
  ///
  /// In en, this message translates to:
  /// **'Failed to Connect'**
  String get connectScreenFailedDialogTitle;

  /// Label for a connection entry for which the user manually entered an IP
  ///
  /// In en, this message translates to:
  /// **'Manual Connection'**
  String get connectScreenManualConnectionLabel;

  /// Label for the button to connect to the most recent engine connection
  ///
  /// In en, this message translates to:
  /// **'Most Recent'**
  String get connectScreenMostRecentButtonLabel;

  /// Label for the button to connect to a new engine instance
  ///
  /// In en, this message translates to:
  /// **'Enter IP Address'**
  String get connectScreenNewConnectionButtonLabel;

  /// Title for the button to connect to a new engine instance
  ///
  /// In en, this message translates to:
  /// **'New Unreal Engine Connection'**
  String get connectScreenNewConnectionButtonTitle;

  /// Title for the modal shown when the user needs to enter a passphrase to finish connecting.
  ///
  /// In en, this message translates to:
  /// **'Remote Control Passphrase'**
  String get connectScreenPassphraseModalTitle;

  /// Message shown when the user is disconnected because their passphrase is no longer valid.
  ///
  /// In en, this message translates to:
  /// **'Disconnected due to invalid passphrase. Please enter the new passphrase.'**
  String get connectScreenPassphraseDisconnectErrorMessage;

  /// Message shown when the user enters an incorrect password.
  ///
  /// In en, this message translates to:
  /// **'Invalid passphrase. Please try again.'**
  String get connectScreenPassphraseIncorrectErrorMessage;

  /// Title for the quick connect panel in the connection screen
  ///
  /// In en, this message translates to:
  /// **'Quick Connect'**
  String get connectScreenQuickConnectPanelTitle;

  /// Message of the dialog shown when the user needs to select a root nDisplay actor before completing the connection
  ///
  /// In en, this message translates to:
  /// **'The current scene contains multiple nDisplay Root Actors. Please select one to control.'**
  String get connectScreenRootActorDialogMessage;

  /// Title of the dialog shown when the user needs to select a root nDisplay actor before completing the connection
  ///
  /// In en, this message translates to:
  /// **'Select nDisplay Root Actor'**
  String get connectScreenRootActorDialogTitle;

  /// Text shown on a button that disconnects from the engine.
  ///
  /// In en, this message translates to:
  /// **'Disconnect'**
  String get disconnectButtonLabel;

  /// Message shown when the user enters an invalid IP address
  ///
  /// In en, this message translates to:
  /// **'Invalid IP address'**
  String get formErrorInvalidIPAddress;

  /// Message shown when the user enters an invalid network port number
  ///
  /// In en, this message translates to:
  /// **'Invalid port'**
  String get formErrorInvalidNetworkPort;

  /// Format for the user-friendly title of a log file.
  ///
  /// In en, this message translates to:
  /// **'{logDate} - Session #{logNumber}'**
  String logTitle(Object logDate, Object logNumber);

  /// Wrapper added to the title of the current log to differentiate it from older logs.
  ///
  /// In en, this message translates to:
  /// **'{logTitle} (Current)'**
  String logTitleCurrentWrapper(Object logTitle);

  /// Label for a generic "Cancel" button in a menu
  ///
  /// In en, this message translates to:
  /// **'Cancel'**
  String get menuButtonCancel;

  /// Label for a generic "Connect" button in a menu
  ///
  /// In en, this message translates to:
  /// **'Connect'**
  String get menuButtonConnect;

  /// Label for a generic "OK" button in a menu
  ///
  /// In en, this message translates to:
  /// **'OK'**
  String get menuButtonOK;

  /// Label for a generic "Proceed" button in a menu
  ///
  /// In en, this message translates to:
  /// **'Proceed'**
  String get menuButtonProceed;

  /// Label for a generic "Retry" button in a menu
  ///
  /// In en, this message translates to:
  /// **'Retry'**
  String get menuButtonRetry;

  /// Label for a generic "Submit" button in a menu.
  ///
  /// In en, this message translates to:
  /// **'Submit'**
  String get menuButtonSubmit;

  /// Semantic label for the barrier area around a modal that dismisses the modal when tapped
  ///
  /// In en, this message translates to:
  /// **'Dismiss'**
  String get modalDismissLabel;

  /// Text shown for a time that passed very recently.
  ///
  /// In en, this message translates to:
  /// **'Just now'**
  String get relativeTimeJustNow;

  /// Text shown for a time that passed seconds ago.
  ///
  /// In en, this message translates to:
  /// **'{seconds} seconds ago'**
  String relativeTimeSecondsAgo(Object seconds);

  /// Text shown for a time that passed minutes ago.
  ///
  /// In en, this message translates to:
  /// **'{minutes} minutes ago'**
  String relativeTimeMinutesAgo(Object minutes);

  /// Text shown for a time that passed over an hour ago.
  ///
  /// In en, this message translates to:
  /// **'Over an hour ago'**
  String get relativeTimeOverAnHourAgo;

  /// Tooltip shown on a button that resets an input box
  ///
  /// In en, this message translates to:
  /// **'Reset'**
  String get resetButtonTooltip;

  /// Generic label for a search bar.
  ///
  /// In en, this message translates to:
  /// **'Search'**
  String get searchBarLabel;

  /// Label for settings menu item that displays/changes the timecode pool/server
  ///
  /// In en, this message translates to:
  /// **'Pool'**
  String get settingsNtpPoolLabel;

  /// Title for the modal dialog shown when changing the NTP timecode pool/server
  ///
  /// In en, this message translates to:
  /// **'NTP Pool'**
  String get settingsNtpPoolTitle;

  /// Title for settings page that displays a single third-party notice
  ///
  /// In en, this message translates to:
  /// **'Third-Party Notice'**
  String get settingsThirdPartyNoticeTitle;

  /// Title for settings menu entry containing licenses for third-party software used in the app
  ///
  /// In en, this message translates to:
  /// **'Third-Party Notices'**
  String get settingsThirdPartyNoticesTitle;

  /// Title for timecode settings dialog
  ///
  /// In en, this message translates to:
  /// **'Timecode'**
  String get settingsTimecodeTitle;

  /// Message shown instead of a timecode when the timecode source provided unusable data
  ///
  /// In en, this message translates to:
  /// **'Error'**
  String get timecodeInvalidBadData;

  /// Message shown instead of a timecode when no data was available
  ///
  /// In en, this message translates to:
  /// **'No Data'**
  String get timecodeInvalidNoData;

  /// Message shown instead of a timecode when we fail to synchronize with the NTP server
  ///
  /// In en, this message translates to:
  /// **'Sync Failed'**
  String get timecodeInvalidNtpSyncFailed;

  /// Label for Timecode settings in the settings dialog
  ///
  /// In en, this message translates to:
  /// **'Timecode'**
  String get timecodeSettingsLabel;

  /// Label for a timecode source originating from an NTP (network time protocol) server
  ///
  /// In en, this message translates to:
  /// **'NTP'**
  String get timecodeSourceNtp;

  /// Label for a timecode source originating from the system's internal clock
  ///
  /// In en, this message translates to:
  /// **'System Time'**
  String get timecodeSourceSystem;
}

class _EpicCommonLocalizationsDelegate extends LocalizationsDelegate<EpicCommonLocalizations> {
  const _EpicCommonLocalizationsDelegate();

  @override
  Future<EpicCommonLocalizations> load(Locale locale) {
    return SynchronousFuture<EpicCommonLocalizations>(lookupEpicCommonLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) => <String>['en'].contains(locale.languageCode);

  @override
  bool shouldReload(_EpicCommonLocalizationsDelegate old) => false;
}

EpicCommonLocalizations lookupEpicCommonLocalizations(Locale locale) {


  // Lookup logic when only language code is specified.
  switch (locale.languageCode) {
    case 'en': return EpicCommonLocalizationsEn();
  }

  throw FlutterError(
    'EpicCommonLocalizations.delegate failed to load unsupported locale "$locale". This is likely '
    'an issue with the localizations generation tool. Please file an issue '
    'on GitHub with a reproducible sample app and the gen-l10n configuration '
    'that was used.'
  );
}
