// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:logging/logging.dart';
import 'package:vector_math/vector_math_64.dart' as vec;

final _log = Logger('JSON');

/// Convert an enum to a value for JSON serialization.
String enumToJsonValue<T extends Enum>(T value) {
  return value.name;
}

/// Convert a serialized JSON value to an enum value.
T? jsonToEnumValue<T extends Enum>(dynamic value, List<T> enumValues) {
  if (!(value is String)) {
    return null;
  }

  return enumValues.firstWhere((enumValue) => enumValue.name == value, orElse: null);
}

/// Create a Vector2 from JSON data.
vec.Vector2? jsonToVector2(dynamic json) {
  if (!(json is Map<String, dynamic>)) {
    return null;
  }

  try {
    return vec.Vector2(json['X'], json['Y']);
  } catch (_) {
    return null;
  }
}

extension Vector2WithJson on vec.Vector2 {
  /// Convert a Vector2 to JSON data.
  Map<String, dynamic> toJson() => {'X': x, 'Y': y};
}
