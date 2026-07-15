// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:collection';
import 'dart:io';
import 'dart:isolate';

import 'package:intl/intl.dart';
import 'package:logging/logging.dart';

import 'log_entry.dart';

final _log = Logger('Log');

/// Log data for a single session of the app.
class Log {
  /// Create a log, optionally passing a list of [entries] to start with.
  Log({Iterable<LogEntry>? entries}) {
    if (entries != null) {
      _entries.addAll(entries);
    }
  }

  /// Pattern to find a date in a log file's name.
  static final _dateFromLogFileName = RegExp(r'(\d\d\d\d_\d\d_\d\d)');

  /// Format for dates in log file names.
  static final _logFileNameDateFormat = DateFormat('yyyy_MM_dd');

  /// The time when the log was created.
  final DateTime startTime = DateTime.timestamp();

  /// Ordered list of entries that occurred in this log.
  final List<LogEntry> _entries = [];

  /// Ordered list of entries that occurred in this log.
  UnmodifiableListView<LogEntry> get entries => UnmodifiableListView(_entries);

  /// Parse a log from a [file].
  /// Returns null if the file couldn't be parsed as a log.
  static Future<Log?> fromFile(File file) async {
    // Try to get the date from the log name. This is a fallback in case the log doesn't have dates inline.
    final RegExpMatch? match = _dateFromLogFileName.firstMatch(file.uri.pathSegments.last);
    final DateTime? fileDate = (match?.groupCount == 1) ? _logFileNameDateFormat.parse(match!.group(1)!) : null;

    // Do this on another thread since it may take a while
    try {
      return await Isolate.run(() => _parseFile(file, fileDate));
    } on Error catch (error, stack) {
      _log.severe('Failed to open log', error, stack);
    }
    return Future.value(null);
  }

  /// Add an entry to the log with a [message], log [level], and associated [loggerName].
  void addEntry({required Level level, required String loggerName, required String message}) {
    _entries.add(LogEntry(
      time: DateTime.timestamp(),
      level: level,
      loggerName: loggerName,
      message: message,
    ));
  }

  @override
  String toString() {
    return entries.join('\n');
  }

  /// Parse the contents of a [file] into a log, or return null if it can't be parsed.
  /// If [fileDate] is not null, it will be used as a fallback date for entries with no inline date.
  static Future<Log?> _parseFile(File file, DateTime? fileDate) async {
    final List<String> lines = await file.readAsLines();

    final List<LogEntry> entries = [];
    String extraLines = '';

    // Function to append any lines in extraLines to the last entry in entries, then clear extraLines.
    final appendExtraLinesToLastEntry = () {
      if (extraLines.isEmpty || entries.isEmpty) {
        return;
      }

      final LogEntry baseEntry = entries.removeLast();

      entries.add(LogEntry(
        time: baseEntry.time,
        level: baseEntry.level,
        loggerName: baseEntry.loggerName,
        message: baseEntry.message + extraLines,
      ));

      extraLines = '';
    };

    // Parse log entries where possible, then append any non-empty lines that don't match to the previous entry.
    // This allows for multi-line entries without an expensive multi-line/backtracking regex to capture them.
    for (final String line in lines) {
      final LogEntry? entry = LogEntry.fromString(line, fallbackDate: fileDate);

      if (entry != null) {
        appendExtraLinesToLastEntry();
        entries.add(entry);
      } else if (line.trim().isNotEmpty) {
        extraLines += '\n$line';
      }
    }

    appendExtraLinesToLastEntry();

    return Log(entries: entries);
  }
}
