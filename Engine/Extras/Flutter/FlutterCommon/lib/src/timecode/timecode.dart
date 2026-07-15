// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/cupertino.dart';
import 'package:intl/intl.dart';

/// Reasons a timecode can be invalid.
enum TimecodeInvalidReason {
  /// Generic failure because no data was available.
  noData,

  /// Generic failure because the data retrieved was unusable.
  badData,

  /// The timecode failed in a way specific to the source type.
  sourceSpecific,
}

/// Represents a frame rate-dependent time for synchronization purposes.
class Timecode {
  const Timecode({
    required this.hours,
    required this.minutes,
    required this.seconds,
    required this.frames,
    required this.fraction,
    this.frameRateNumerator = defaultFrameRateNumerator,
    this.frameRateDenominator = defaultFrameRateDenominator,
    this.dropFrame = false,
  })  : this.invalidReason = null,
        getSourceSpecificInvalidMessage = null;

  /// Create an invalid time code with the given [invalidReason].
  const Timecode.invalid(
    this.invalidReason, {
    this.getSourceSpecificInvalidMessage,
  })  : hours = 0,
        minutes = 0,
        seconds = 0,
        frames = 0,
        fraction = 0,
        this.frameRateNumerator = defaultFrameRateNumerator,
        this.frameRateDenominator = defaultFrameRateDenominator,
        dropFrame = false;

  /// Create a timecode with zero for all values and the default frame rate.
  static const Timecode zero = Timecode(hours: 0, minutes: 0, seconds: 0, frames: 0, fraction: 0);

  /// Create a timecode from a [dateTime].
  static Timecode fromDateTime(
    DateTime dateTime, {
    frameRateNumerator = defaultFrameRateNumerator,
    frameRateDenominator = defaultFrameRateDenominator,
    dropFrame = false,
  }) {
    final int microseconds = (dateTime.millisecond * (Duration.microsecondsPerMillisecond)) + dateTime.microsecond;
    final double frameWithFraction =
        (microseconds / Duration.microsecondsPerSecond) * (frameRateNumerator / frameRateDenominator);

    return Timecode(
      hours: dateTime.hour,
      minutes: dateTime.minute,
      seconds: dateTime.second,
      frames: frameWithFraction.floor(),
      fraction: frameWithFraction.remainder(1),
    );
  }

  /// Create a timecode from a number of seconds.
  static Timecode fromSeconds(
    double seconds, {
    frameRateNumerator = defaultFrameRateNumerator,
    frameRateDenominator = defaultFrameRateDenominator,
    dropFrame = false,
  }) {
    if (seconds.isNaN || seconds.isInfinite) {
      return Timecode.invalid(TimecodeInvalidReason.badData);
    }

    final double frameWithFraction = seconds.remainder(1) * (frameRateNumerator / frameRateDenominator);
    final duration = Duration(microseconds: (Duration.microsecondsPerSecond * seconds).round());

    return Timecode(
      hours: duration.inHours % Duration.hoursPerDay,
      minutes: duration.inMinutes % Duration.minutesPerHour,
      seconds: duration.inSeconds % Duration.secondsPerMinute,
      frames: frameWithFraction.floor(),
      fraction: frameWithFraction.remainder(1),
    );
  }

  /// The numerator of the default frame rate expressed as a fraction.
  static const int defaultFrameRateNumerator = 60;

  /// The denominator of the default frame rate expressed as a fraction.
  static const int defaultFrameRateDenominator = 1;

  /// Hours elapsed.
  final int hours;

  /// Minutes elapsed.
  final int minutes;

  /// Seconds elapsed.
  final int seconds;

  /// Whole-number frames elapsed (rounded down).
  final int frames;

  /// Remaining fraction of frames elapsed after rounding down.
  final double fraction;

  /// Whether this is a drop frame timecode.
  final bool dropFrame;

  /// The numerator of the frame rate expressed as a fraction.
  final int frameRateNumerator;

  /// The denominator of the frame rate expressed as a fraction.
  final int frameRateDenominator;

  /// The reason this timecode is invalid. If null, this timecode is valid.
  final TimecodeInvalidReason? invalidReason;

  /// Returns a source-specific error message localized to the provided [context].
  /// Only used if [invalidReason] is [TimecodeInvalidReason.sourceSpecific].
  final String Function(BuildContext)? getSourceSpecificInvalidMessage;

  /// Whether the timecode is valid.
  bool get bIsValid => invalidReason == null;

  @override
  String toString() {
    final int maxFrames = (frameRateNumerator / frameRateDenominator).floor();
    final int framePlaces = maxFrames.toString().length;

    final twoDigits = NumberFormat('00');
    final frameSeparator = dropFrame ? ';' : ':';
    return '${twoDigits.format(hours)}'
        ':${twoDigits.format(minutes)}'
        ':${twoDigits.format(seconds)}'
        '$frameSeparator${frames.toString().padLeft(framePlaces, '0')}';
  }

  /// Convert the timecode to a string which includes the fraction of the frame.
  String toStringWithFraction() {
    final threeDigits = NumberFormat('000');
    final int fractionTruncated = (fraction * 1000).truncate();
    return '$this.${threeDigits.format(fractionTruncated)}';
  }

  /// Convert the timecode to a floating-point number of seconds (since 00:00:00.000)
  double toSeconds() {
    if (!bIsValid) {
      return 0;
    }

    return (hours * Duration.secondsPerHour) +
        (minutes * Duration.secondsPerMinute) +
        seconds +
        ((frames + fraction) * (frameRateDenominator / frameRateNumerator));
  }
}
