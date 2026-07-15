// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../utilities/json.dart';

/// Extensions to serialize/deserialize new types of preference values.
extension GenericPreferenceAdapterExtensions on StreamingSharedPreferences {
  /// Starts with the current String set value for the given [key], then emits a new value every time there are changes
  /// to the value associated with [key].
  ///
  /// If the value is null, starts with the value provided in [defaultValue]. When the value transitions from non-null
  /// to null (ie. when the value is removed), emits [defaultValue].
  Preference<Set<String>> getStringSet(
    String key, {
    required Set<String> defaultValue,
  }) {
    return getCustomValue(key, defaultValue: defaultValue, adapter: StringSetAdapter.instance);
  }

  /// Starts with the current Enum value for the given [key], then emits a new value every time there are changes to the
  /// value associated with [key].
  ///
  /// If the value is null, starts with the value provided in [defaultValue]. When the value transitions from non-null
  /// to null (ie. when the value is removed), emits [defaultValue].
  Preference<T> getEnum<T extends Enum>(
    String key, {
    required T defaultValue,
    required List<T> enumValues,
  }) {
    return getCustomValue(key, defaultValue: defaultValue, adapter: EnumAdapter(enumValues));
  }
}

/// A [PreferenceAdapter] implementation for storing and retrieving a [Set] of [String] objects.
class StringSetAdapter extends PreferenceAdapter<Set<String>> {
  static const instance = StringSetAdapter._();
  const StringSetAdapter._();

  @override
  Set<String>? getValue(preferences, key) => preferences.getStringList(key)?.toSet();

  @override
  Future<bool> setValue(preferences, key, values) => preferences.setStringList(key, values.toList(growable: false));
}

/// A [PreferenceAdapter] implementation for storing and retrieving an [Enum] value as a string.
class EnumAdapter<T extends Enum> extends PreferenceAdapter<T> {
  const EnumAdapter(this.enumValues);

  final List<T> enumValues;

  @override
  T? getValue(preferences, key) {
    try {
      final String? name = preferences.getString(key);

      if (name == null) {
        return null;
      }

      return jsonToEnumValue(name, enumValues);
    } catch (_) {
      return null;
    }
  }

  @override
  Future<bool> setValue(preferences, key, value) => preferences.setString(key, enumToJsonValue(value));
}
