// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:intl/intl.dart';
import 'package:logging/logging.dart';

/// Contains data about a single logged message.
class LogEntry {
  const LogEntry({
    required this.time,
    required this.level,
    required this.loggerName,
    required this.message,
  });

  /// Format for displaying the log entry's time of day.
  static final _timeFormat = DateFormat('HH:mm:ss.SSS');

  /// Format for displaying the log entry's date.
  static final _dateFormat = DateFormat('yyyy-MM-dd');

  /// Regular expression that matches the first line of a log entry in a log file.
  static final _entryPattern = RegExp(r'\[(?:(\d\d\d\d-\d\d-\d\d) )?(\d\d:\d\d:\d\d.\d\d\d)\] (.+?)/(.*?): ?(.*)');

  /// The time at which this was logged.
  final DateTime time;

  /// The log level of this entry.
  final Level level;

  /// The name of the logger that logged this message.
  final String loggerName;

  /// The message that was logged.
  final String message;

  /// Parse a log entry from a [string]. The string must contain only a single log entry.
  /// If the log entry contains no date, the entry's date will be set to the [fallbackDate] provided.
  /// If the [fallbackDate] provided is also null, today's date will be used.
  /// Returns null if the string couldn't be parsed as a log entry.
  static LogEntry? fromString(String string, {DateTime? fallbackDate}) {
    final Match? match = _entryPattern.matchAsPrefix(string);
    if (match == null) {
      return null;
    }

    final String? message = match.group(5);
    if (message == null) {
      return null;
    }

    final String? loggerName = match.group(4);
    if (loggerName == null) {
      return null;
    }

    final Level level;
    try {
      level = Level.LEVELS.firstWhere((level) => level.name == match.group(3));
    } on StateError {
      return null;
    }

    final DateTime? parsedTime = _parseDateTime(match.group(2), _timeFormat);
    if (parsedTime == null) {
      return null;
    }

    final DateTime parsedDate = _parseDateTime(match.group(1), _dateFormat) ?? fallbackDate ?? DateTime.now();
    final DateTime dateTime = DateTime(
      parsedDate.year,
      parsedDate.month,
      parsedDate.day,
      parsedTime.hour,
      parsedTime.minute,
      parsedTime.second,
      parsedTime.millisecond,
      parsedTime.microsecond,
    );

    return LogEntry(
      time: dateTime,
      level: level,
      loggerName: loggerName,
      message: message,
    );
  }

  /// Parse a date/time from a [string] in the given [format], returning null if it can't be parsed or the string is
  /// null.
  static DateTime? _parseDateTime(String? string, DateFormat format) {
    if (string == null) {
      return null;
    }

    try {
      return format.parse(string);
    } catch (FormatException) {
      return null;
    }
  }

  @override
  String toString() {
    return '[${_dateFormat.format(time)} ${_timeFormat.format(time)}] ${level.name}/${loggerName}: ${message}';
  }
}
