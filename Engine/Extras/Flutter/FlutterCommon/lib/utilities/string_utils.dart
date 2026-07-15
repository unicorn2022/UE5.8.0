// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:math';

/// Make a string that represents the [byteCount] passed in metric units (e.g. 3 KB for 3000 bytes, or 5.4 MB for 5400).
String byteCountToString(int byteCount) {
  /// List of tuples of (unit symbol, decimal places), where the index is the exponent on 1000 of the value's magnitude
  const unitsAndFractionDigits = [
    ('B', 0),
    ('KB', 0),
    ('MB', 1),
    ('GB', 2),
    ('TB', 2),
    ('PB', 2),
  ];

  final int absByteCount = byteCount.abs();

  int exponent = 1;
  while (absByteCount >= pow(1000, exponent)) {
    ++exponent;
  }

  // Return to the smallest magnitude before we passed the actual byte count, or the highest unit, whichever is lowest
  exponent = min(exponent, unitsAndFractionDigits.length) - 1;

  final double fraction = byteCount / pow(1000, exponent);
  final (String unit, int fractionDigits) = unitsAndFractionDigits[exponent];

  return '${fraction.toStringAsFixed(fractionDigits)} $unit';
}
