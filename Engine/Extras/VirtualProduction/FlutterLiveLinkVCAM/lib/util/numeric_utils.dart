// Copyright Epic Games, Inc. All Rights Reserved.

/// Contains static functions and values for working with numbers of specific bit sizes.
class NumericUtils {
  /// Minimum value of an unsigned 8-bit integer.
  static const int min8BitUInt = 0;

  /// Maximum value of an unsigned 8-bit integer.
  static const int max8BitUInt = (1 << 8) - 1;

  /// Minimum value of an unsigned 16-bit integer.
  static const int min16BitUInt = 0;

  /// Maximum value of an unsigned 16-bit integer.
  static const int max16BitUInt = (1 << 16) - 1;

  /// Check that an integer is in the valid range for an 8-bit unsigned integer.
  static bool isValid8BitUInt(int value) {
    return value >= min8BitUInt && value <= max8BitUInt;
  }

  /// Check that an integer is in the valid range for a 16-bit unsigned integer.
  static bool isValid16BitUInt(int value) {
    return value >= min16BitUInt && value <= max16BitUInt;
  }
}
