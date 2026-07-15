// Copyright Epic Games, Inc. All Rights Reserved.
// Generated automatically by flutter_gen.

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart' as intl;

import 'tentacle_localizations_en.dart';

// ignore_for_file: type=lint

/// Callers can lookup localized strings with an instance of TentacleLocalizations
/// returned by `TentacleLocalizations.of(context)`.
///
/// Applications need to include `TentacleLocalizations.delegate()` in their app's
/// `localizationDelegates` list, and the locales they support in the app's
/// `supportedLocales` list. For example:
///
/// ```dart
/// import 'generated/tentacle_localizations.dart';
///
/// return MaterialApp(
///   localizationsDelegates: TentacleLocalizations.localizationsDelegates,
///   supportedLocales: TentacleLocalizations.supportedLocales,
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
/// be consistent with the languages listed in the TentacleLocalizations.supportedLocales
/// property.
abstract class TentacleLocalizations {
  TentacleLocalizations(String locale) : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static TentacleLocalizations? of(BuildContext context) {
    return Localizations.of<TentacleLocalizations>(context, TentacleLocalizations);
  }

  static const LocalizationsDelegate<TentacleLocalizations> delegate = _TentacleLocalizationsDelegate();

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

  /// Label for settings menu item representing a connection to no device
  ///
  /// In en, this message translates to:
  /// **'Disconnected'**
  String get settingsTentacleSyncDeviceDisconnected;

  /// Label for settings menu item that displays/changes the connected Tentacle device
  ///
  /// In en, this message translates to:
  /// **'Device'**
  String get settingsTentacleSyncDeviceLabel;

  /// Title for the modal dialog that lists and lets the user select a Tentacle device
  ///
  /// In en, this message translates to:
  /// **'Tentacle Device'**
  String get settingsTentacleSyncDeviceTitle;

  /// Text shown in place of a device's name when no device is selected
  ///
  /// In en, this message translates to:
  /// **'Disconnected'**
  String get settingsTentacleSyncNoDeviceName;

  /// Text shown in place of the list of available Tentacle devices when none are detected
  ///
  /// In en, this message translates to:
  /// **'No devices detected.'**
  String get settingsTentacleSyncNoDevicesFoundLabel;

  /// Label for a Tentacle device's battery level.
  ///
  /// In en, this message translates to:
  /// **'Battery'**
  String get settingsTentacleDetailsBatteryLabel;

  /// Label for the settings menu item that connects to a Tentacle device.
  ///
  /// In en, this message translates to:
  /// **'Connect'**
  String get settingsTentacleDetailsConnectLabel;

  /// Label for the settings menu item that disconnects from a Tentacle device.
  ///
  /// In en, this message translates to:
  /// **'Disconnect'**
  String get settingsTentacleDetailsDisconnectLabel;

  /// Label for a Tentacle device's frame rate.
  ///
  /// In en, this message translates to:
  /// **'Frame Rate'**
  String get settingsTentacleDetailsFrameRateLabel;

  /// Label for the last time Tentacle device was seen.
  ///
  /// In en, this message translates to:
  /// **'Last Seen'**
  String get settingsTentacleDetailsLastSeenLabel;

  /// Label for a Tentacle device's mode of operation.
  ///
  /// In en, this message translates to:
  /// **'Mode'**
  String get settingsTentacleDetailsModeLabel;

  /// Label for a Tentacle device's signal strength.
  ///
  /// In en, this message translates to:
  /// **'Signal Strength'**
  String get settingsTentacleDetailsSignalStrengthLabel;

  /// Label for a Tentacle device's timecode.
  ///
  /// In en, this message translates to:
  /// **'Timecode'**
  String get settingsTentacleDetailsTimecodeLabel;

  /// Text shown instead of device details when the detail is unknown because the device is unavailable.
  ///
  /// In en, this message translates to:
  /// **'Unknown'**
  String get settingsTentacleDetailsUnknown;

  /// Label for the Tentacle device mode that flashes a green light.
  ///
  /// In en, this message translates to:
  /// **'Green (Generate)'**
  String get tentacleModeLabelGreen;

  /// Label for the Tentacle device mode that flashes a red light.
  ///
  /// In en, this message translates to:
  /// **'Red (Link)'**
  String get tentacleModeLabelRed;

  /// Label for a Tentacle device with no specific product ID
  ///
  /// In en, this message translates to:
  /// **'Tentacle Device'**
  String get tentacleProductIdGeneric;

  /// Label for a Tentacle SYNC E device
  ///
  /// In en, this message translates to:
  /// **'Tentacle SYNC E'**
  String get tentacleProductIdSyncE;

  /// Label for a Tentacle TIMEBAR device
  ///
  /// In en, this message translates to:
  /// **'Tentacle TIMEBAR'**
  String get tentacleProductIdTimebar;

  /// Label for a Tentacle TRACK E device
  ///
  /// In en, this message translates to:
  /// **'Tentacle TRACK E'**
  String get tentacleProductIdTrackE;

  /// Label for the Tentacle device signal strength when it's the highest quality.
  ///
  /// In en, this message translates to:
  /// **'Excellent'**
  String get tentacleSignalStrengthExcellent;

  /// Label for the Tentacle device signal strength when it's better than poor, but worse than good.
  ///
  /// In en, this message translates to:
  /// **'Fair'**
  String get tentacleSignalStrengthFair;

  /// Label for the Tentacle device signal strength when it's better than fair, but worse than excellent.
  ///
  /// In en, this message translates to:
  /// **'Good'**
  String get tentacleSignalStrengthGood;

  /// Label for the Tentacle device signal strength when it's the lowest quality.
  ///
  /// In en, this message translates to:
  /// **'Poor'**
  String get tentacleSignalStrengthPoor;

  /// Message shown instead of a timecode when no Tentacle device is available
  ///
  /// In en, this message translates to:
  /// **'No Device'**
  String get timecodeInvalidNoTentacleDevice;

  /// Label for a timecode source originating from a Tentacle Sync device
  ///
  /// In en, this message translates to:
  /// **'Tentacle Sync'**
  String get timecodeSourceTentacle;
}

class _TentacleLocalizationsDelegate extends LocalizationsDelegate<TentacleLocalizations> {
  const _TentacleLocalizationsDelegate();

  @override
  Future<TentacleLocalizations> load(Locale locale) {
    return SynchronousFuture<TentacleLocalizations>(lookupTentacleLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) => <String>['en'].contains(locale.languageCode);

  @override
  bool shouldReload(_TentacleLocalizationsDelegate old) => false;
}

TentacleLocalizations lookupTentacleLocalizations(Locale locale) {


  // Lookup logic when only language code is specified.
  switch (locale.languageCode) {
    case 'en': return TentacleLocalizationsEn();
  }

  throw FlutterError(
    'TentacleLocalizations.delegate failed to load unsupported locale "$locale". This is likely '
    'an issue with the localizations generation tool. Please file an issue '
    'on GitHub with a reproducible sample app and the gen-l10n configuration '
    'that was used.'
  );
}
