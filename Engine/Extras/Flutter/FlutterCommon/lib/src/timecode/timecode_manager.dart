// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:collection';

import 'package:flutter/widgets.dart';
import 'package:flutter/foundation.dart';
import 'package:logging/logging.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../preferences.dart';
import '../widgets/timecode/timecode_settings.dart';
import 'ntp_timecode.dart';
import 'system_clock_timecode.dart';
import 'timecode.dart';
import 'timecode_source.dart';
import 'timecode_synchronizer.dart';

final _log = Logger('TimecodeManager');

/// Provides the current timecode and handles keeping it in sync with the timecode source.
class TimecodeManager {
  TimecodeManager({
    required this.context,
    required PreferencesBundle preferences,
  })  : _fallbackSource = SystemClockTimecodeSource(),
        sourceName = preferences.persistent.getString(
          'timecode.source',
          defaultValue: SystemClockTimecodeSource.staticInternalName,
        ) {
    registerSource(_fallbackSource);
    registerSource(NtpTimecodeSource());
  }

  /// The context in which this is retrieving timecodes.
  final BuildContext context;

  /// The name of the selected synchronization source for the timecode.
  final Preference<String> sourceName;

  /// List of stream subscriptions to cancel on dispose.
  final List<StreamSubscription> _subscriptions = [];

  /// Map from internal source name to the source itself.
  final Map<String, TimecodeSource> _sources = {};

  /// The source to use if we fail to build another one for any reason.
  final TimecodeSource _fallbackSource;

  /// The synchronizer that keeps the timecode in sync with the current source.
  /// This should only be null when [initialize] has not yet been called.
  TimecodeSynchronizer? _synchronizer;

  /// The current timecode.
  Timecode get timecode => _synchronizer?.timecode ?? Timecode.invalid(TimecodeInvalidReason.noData);

  /// Map from internal name of an available timecode source to information about the source.
  UnmodifiableMapView<String, TimecodeSource> get sources => UnmodifiableMapView(_sources);

  /// Convenience function to get the current timecode source as a stream.
  ValueListenable<TimecodeSource> get activeSource => _activeSource;
  late final _activeSource = ValueNotifier<TimecodeSource>(_fallbackSource);

  /// True if [initialize] has been called.
  bool get _bIsInitialized => _synchronizer != null;

  /// Register a new timecode source that the user can select.
  void registerSource(TimecodeSource newSource) {
    if (_bIsInitialized) {
      _log.severe('Timecode sources must be registered before initialize() is called');
      return;
    }

    _sources[newSource.internalName] = newSource;
  }

  /// Initialize the manager.
  /// MUST be called after all calls to [registerSource] and before any other calls are made to this class.
  void initialize() {
    if (_bIsInitialized) {
      _log.severe('Timecode manager is already initialized');
      return;
    }

    _recreateSynchronizer();

    _subscriptions.add(
      sourceName.listen((_) => _recreateSynchronizer()),
    );
  }

  /// Clean up any resources used by the manager.
  void dispose() {
    _activeSource.dispose();
    _synchronizer?.dispose();
    _subscriptions.forEach((subscription) => subscription.cancel());
  }

  /// Given a [route] path, return the contents of the timecode-related settings page, if it exists.
  Widget? getTimecodeSettingsPage(String route) {
    switch (route) {
      case SettingsTimecodeSourcePicker.route:
        return const SettingsTimecodeSourcePicker();
    }

    for (final TimecodeSource source in _sources.values) {
      final Widget? sourcePage = source.getSettingsPage?.call(route);
      if (sourcePage != null) {
        return sourcePage;
      }
    }

    return null;
  }

  /// Clean up the previous timecode source and create a new one.
  void _recreateSynchronizer() {
    _synchronizer?.dispose();

    final String sourceNameString = sourceName.getValue();
    TimecodeSource? source = _sources[sourceNameString];
    if (source == null) {
      _log.severe('Timecode source "$sourceNameString" is unsupported; falling back to default');
      source = _fallbackSource;
    }

    _synchronizer = source.makeSynchronizer(context);
    _activeSource.value = source;
  }
}
