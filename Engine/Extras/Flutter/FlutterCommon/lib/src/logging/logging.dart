// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:logging/logging.dart';
import 'package:path_provider/path_provider.dart';

import '../../localizations.dart';

final _timeFormatter = DateFormat('HH:mm:ss.sss');
final _fileDateFormatter = DateFormat('yyyy_MM_dd');
final _friendlyDateFormatter = DateFormat('yyyy/MM/dd');

final _log = Logger('Logging');

/// Color tokens to prepend to logs at each level.
final Map<Level, String> _logColorTokens = {
  Level.CONFIG: '\x1B[32m',
  Level.INFO: '\x1B[34m',
  Level.WARNING: '\x1B[93m',
  Level.SEVERE: '\x1B[91m',
  Level.SHOUT: '\x1B[95m',
};

/// Data stored in the name of a log.
class _LogNameData {
  const _LogNameData({
    required this.date,
    required this.sessionNumber,
  });

  /// The date on which the log was started.
  final DateTime date;

  /// The session number for that date.
  final int sessionNumber;
}

class Logging {
  /// Singleton instance of this class.
  static Logging instance = Logging();

  /// Whether logging has been initialized.
  bool _bIsInitialized = false;

  /// A future that will return [_logFileSink] once it's created.
  Future<IOSink>? _logFileSinkFuture;

  /// Name to prepend to log files.
  late final String _appName;

  /// Number of log files to retain.
  late final int _logHistory;

  /// Max age of a log before it should be deleted.
  late final Duration? _logRetention;

  /// Regex to parse a log's name.
  late final RegExp _logNameParser;

  /// Given a [logFile], make a user-friendly string to refer to it by using the [context]'s localization data.
  /// If [bIsCurrent] is true, append a label indicating that this is the current log.
  static String makeNameForLog({required BuildContext context, required File logFile, bool bIsCurrent = false}) {
    final _LogNameData? nameData = _parseLogName(logFile);

    if (nameData == null) {
      return logFile.uri.pathSegments.last;
    }

    // Format the title
    final String title = EpicCommonLocalizations.of(context)!.logTitle(
      _friendlyDateFormatter.format(nameData.date),
      nameData.sessionNumber,
    );

    if (!bIsCurrent) {
      return title;
    }

    return EpicCommonLocalizations.of(context)!.logTitleCurrentWrapper(title);
  }

  /// Get a list of log files associated with the app in reverse chronological order.
  static Future<List<File>> getAppLogFiles() async {
    final Directory logDirectory = await Logging.instance.getLogDirectory();
    final List<File> files =
        await logDirectory.list(recursive: false).map((FileSystemEntity entity) => File(entity.path)).toList();

    final Map<File, _LogNameData?> logNameData = {
      for (final file in files) file: _parseLogName(file),
    };

    // Sort so the most recent files appear first
    files.sort((File fileA, File fileB) {
      final _LogNameData? fileAData = logNameData[fileA];
      final _LogNameData? fileBData = logNameData[fileB];

      if (fileAData == null || fileBData == null) {
        return fileB.path.compareTo(fileA.path);
      }

      final int dateComparison = fileBData.date.compareTo(fileAData.date);
      if (dateComparison != 0) {
        return dateComparison;
      }

      return fileBData.sessionNumber.compareTo(fileAData.sessionNumber);
    });

    return files;
  }

  /// Initialize logging for the app.
  /// Log filenames will use a combination of the [appName] and the date on which the log started, and up to
  /// [logHistory] logs will be kept before deleting the oldest log.
  /// If [logRetention] is set, any logs older than this duration will be deleted on initialization.
  void initialize(String appName, {int logHistory = 10, Duration? logRetention = const Duration(days: 30)}) {
    if (_bIsInitialized) {
      _log.warning('Tried to initialize logging more than once');
      return;
    }

    _logNameParser = RegExp(appName + r'_(\d\d\d\d_\d\d_\d\d)(?:_(\d+))?\.log');

    _appName = appName;
    _logHistory = logHistory;
    _logRetention = logRetention;

    _bIsInitialized = true;

    Logger.root.level = Level.ALL;
    Logger.root.onRecord.listen(_onLogEvent);
  }

  /// Clean up logging for the app.
  void cleanUp() {
    _logFileSinkFuture?.then((sink) => sink.close());
    _bIsInitialized = false;
  }

  /// Get the directory in which to store logs.
  Future<Directory> getLogDirectory() async {
    final Directory documentRoot = await getApplicationSupportDirectory();

    return Directory('${documentRoot.path}/logs');
  }

  /// Parse data stored in a log file's name.
  /// Returns null if the log name couldn't be parsed.
  static _LogNameData? _parseLogName(File logFile) {
    final match = instance._logNameParser.matchAsPrefix(logFile.uri.pathSegments.last);

    if (match == null || match.groupCount == 0) {
      return null;
    }

    // Parse date
    final DateTime logDate;
    try {
      logDate = _fileDateFormatter.parse(match.group(1)!);
    } catch (error) {
      return null;
    }

    // Parse session number
    int session = 1;
    final String? sessionString = match.group(2);
    if (sessionString != null) {
      session = int.tryParse(sessionString) ?? session;
    }

    return _LogNameData(
      date: logDate,
      sessionNumber: session,
    );
  }

  /// Start a new log file and return the IO sink to write to it.
  Future<IOSink> _startNewLogFile() async {
    final Directory logDirectory = await getLogDirectory();

    try {
      await logDirectory.create(recursive: true);
    } catch (e) {
      return Future.error(e);
    }

    final baseFileName = '${_appName}_${_fileDateFormatter.format(DateTime.now())}';

    // Find the log for this date with the highest index for today's date
    final List<File> logFiles = await getAppLogFiles();

    int highestSessionIndex = 0;
    for (final existingFile in logFiles) {
      final int? sessionIndex = _parseLogName(existingFile)?.sessionNumber;

      if (sessionIndex != null && sessionIndex > highestSessionIndex) {
        highestSessionIndex = sessionIndex;
      }
    }

    // Increment the session number and create a new log there
    final logFile = File('${logDirectory.path}/${baseFileName}_${highestSessionIndex + 1}.log');
    final IOSink newSink = logFile.openWrite(mode: FileMode.append);

    // Now that we've created a log, delete any extras beyond our max log history/retention
    await _cleanUpOldLogs();

    return newSink;
  }

  /// Remove any logs outside of the log history or retention ranges.
  Future<void> _cleanUpOldLogs() async {
    final List<File> logFiles = await getAppLogFiles();

    // Delete old logs until there are fewer than [_logHistory]
    while (logFiles.length > _logHistory) {
      final File oldestLog = logFiles.removeLast();
      try {
        await oldestLog.delete();
      } catch (e) {
        _log.warning('Failed to delete old log "${oldestLog.path}"', e);
      }
    }

    // Delete any logs that have passed the retention date
    if (_logRetention != null) {
      final DateTime now = DateTime.now();
      for (int logIndex = logFiles.length - 1; logIndex >= 0; --logIndex) {
        final File log = logFiles[logIndex];
        final _LogNameData? logNameData = _parseLogName(log);

        if (logNameData == null || now.difference(logNameData.date) > _logRetention!) {
          logFiles.removeAt(logIndex);
          await log.delete();
        }
      }
    }
  }

  /// Called when a log event is received.
  void _onLogEvent(LogRecord record) {
    String formatted =
        '[${_timeFormatter.format(record.time)}] ${record.level.name}/${record.loggerName}: ${record.message}';
    if (record.error != null) {
      formatted += '\n${record.error}';
    }

    if (record.stackTrace != null) {
      formatted += '\n${record.stackTrace}';
    }

    _logToConsole(formatted, record);

    if (!kIsWeb) {
      _logToFile(formatted, record);
    }
  }

  /// Send a log to the debug console.
  void _logToConsole(String formatted, LogRecord record) {
    const resetToken = '\x1B[0m';
    final String colorToken = _logColorTokens[record.level] ?? resetToken;
    print('$colorToken$formatted$resetToken');
  }

  /// Send a log to the log file.
  void _logToFile(String formatted, LogRecord record) async {
    if (_logFileSinkFuture == null) {
      _logFileSinkFuture = _startNewLogFile();
    }

    final IOSink sink = await _logFileSinkFuture!;
    sink.writeln(formatted);
  }
}
