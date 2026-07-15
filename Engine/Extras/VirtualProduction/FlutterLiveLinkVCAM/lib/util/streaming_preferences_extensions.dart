// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:convert';

import 'package:logging/logging.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import 'net_utilities.dart';

final _log = Logger('PrefExtensions');

/// Extensions to serialize/deserialize new types of preference values.
extension PreferenceAdapterExtensions on StreamingSharedPreferences {
  /// Starts with the current list value for the given [key], then emits a new value every time there are changes to the
  /// value associated with [key].
  Preference<EngineConnectionData> getEngineConnectionData(String key) {
    return getCustomValue(
      key,
      defaultValue: EngineConnectionData.invalid,
      adapter: EngineConnectionDataAdapter.instance,
    );
  }
}

/// A [PreferenceAdapter] implementation for storing and retrieving a [List] of [RecentlyPlacedActorData] values.
class EngineConnectionDataAdapter extends PreferenceAdapter<EngineConnectionData> {
  static const instance = EngineConnectionDataAdapter._();
  const EngineConnectionDataAdapter._();

  @override
  EngineConnectionData? getValue(SharedPreferences preferences, String key) {
    try {
      final String? jsonString = preferences.getString(key);
      if (jsonString == null) {
        return null;
      }
      return EngineConnectionData.fromJson(json.decode(jsonString));
    } catch (e) {
      _log.warning('Failed to deserialize RecentlyPlacedActorData list: $e');
      return null;
    }
  }

  @override
  Future<bool> setValue(SharedPreferences preferences, String key, EngineConnectionData value) {
    if (!value.bIsValid) {
      return preferences.setString(key, '');
    }

    return preferences.setString(key, json.encode(value.toJson()));
  }
}
