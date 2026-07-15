import 'dart:collection';
import 'dart:math';

import 'package:flutter/material.dart';

import '../../webrtc/api/webrtc_api.g.dart';

/// Processes WebRTC statistic reports and maintains a historical list of stat values.
class WebRtcStatHistory extends ChangeNotifier {
  WebRtcStatHistory({this.maxHistory = 120});

  /// Number of bytes in one megabit.
  static const double bytesPerMegabit = 125000.0;

  /// The number of historical entries to maintain per stat. If more than this many are stored, the oldest entry will be
  /// removed from the start of the lists.
  final int maxHistory;

  /// History of jitter buffer delay in seconds.
  final List<double> _jitterBufferDelayHistory = [];

  /// History of packet jitter in seconds.
  final List<double> _jitterHistory = [];

  /// History of frame rate values.
  final List<double> _framesPerSecondHistory = [];

  /// History of total frames received.
  final List<int> _framesReceivedHistory = [];

  /// History of total frames which were assembled from multiple packets.
  final List<int> _multiFramePacketCountHistory = [];

  /// History of total freezes experienced.
  final List<int> _freezeCountHistory = [];

  /// History of total negative acknowledgment packets sent.
  final List<int> _nackCountHistory = [];

  /// History of total picture loss indication packets sent.
  final List<int> _pliCountHistory = [];

  /// History of total full intra request packets sent.
  final List<int> _firCountHistory = [];

  /// History of bitrate values in megabits per second.
  final List<double> _bitrateHistory = [];

  /// History of total packets lost.
  final List<int> _packetsLostHistory = [];

  /// History of total keyframes decoded.
  final List<int> _keyframesDecodedHistory = [];

  /// History of frames decoded per update.
  final List<double> _decodeFrameRateHistory = [];

  /// History of total frames dropped.
  final List<int> _framesDroppedHistory = [];

  /// History of inter-frame delay in seconds.
  final List<double> _interFrameDelayHistory = [];

  /// History of processing delay in seconds.
  final List<double> _processingDelayHistory = [];

  /// History of total duration of all freezes experienced.
  final List<double> _totalFreezesDurationHistory = [];

  /// History of time spent decoding per update.
  final List<double> _decodeTimeHistory = [];

  /// History of total packets received.
  final List<int> _packetsReceivedHistory = [];

  /// The timestamp when the last update occurred.
  double? _lastTimestamp;

  /// The number of total bytes received on the last update.
  BigInt _lastBytesReceived = BigInt.zero;

  /// The number of total frames decoded on the last update.
  int _lastFramesDecoded = 0;

  /// The total inter-frame delay on the last update in seconds.
  double _lastTotalInterFrameDelay = 0;

  /// The total processing delay on the last update in seconds.
  double _lastTotalProcessingDelay = 0;

  /// The total decode time on the last update in seconds.
  double _lastTotalDecodeTime = 0;

  /// History of jitter buffer delay in seconds.
  UnmodifiableListView<double> get jitterBufferDelayHistory => UnmodifiableListView(_jitterBufferDelayHistory);

  /// History of packet jitter in seconds.
  UnmodifiableListView<double> get jitterHistory => UnmodifiableListView(_jitterHistory);

  /// History of frame rate values.
  UnmodifiableListView<double> get framesPerSecondHistory => UnmodifiableListView(_framesPerSecondHistory);

  /// History of total frames received.
  UnmodifiableListView<int> get framesReceivedHistory => UnmodifiableListView(_framesReceivedHistory);

  /// History of total frames which were assembled from multiple packets.
  UnmodifiableListView<int> get multiFramePacketCountHistory => UnmodifiableListView(_multiFramePacketCountHistory);

  /// History of total freezes experienced.
  UnmodifiableListView<int> get freezeCountHistory => UnmodifiableListView(_freezeCountHistory);

  /// History of total negative acknowledgment packets sent.
  UnmodifiableListView<int> get nackCountHistory => UnmodifiableListView(_nackCountHistory);

  /// History of total picture loss indication packets sent.
  UnmodifiableListView<int> get pliCountHistory => UnmodifiableListView(_pliCountHistory);

  /// History of total full intra request packets sent.
  UnmodifiableListView<int> get firCountHistory => UnmodifiableListView(_firCountHistory);

  /// History of total duration of freezes experienced.
  UnmodifiableListView<double> get totalFreezesDurationHistory => UnmodifiableListView(_totalFreezesDurationHistory);

  /// History of time spent decoding per update.
  UnmodifiableListView<double> get decodeTimeHistory => UnmodifiableListView(_decodeTimeHistory);

  /// History of total packets received.
  UnmodifiableListView<int> get packetsReceivedHistory => UnmodifiableListView(_packetsReceivedHistory);

  /// History of bitrate values in megabits per second.
  UnmodifiableListView<double> get bitrateHistory => UnmodifiableListView(_bitrateHistory);

  /// History of total packets lost.
  UnmodifiableListView<int> get packetsLostHistory => UnmodifiableListView(_packetsLostHistory);

  /// History of total keyframes decoded.
  UnmodifiableListView<int> get keyframesDecodedHistory => UnmodifiableListView(_keyframesDecodedHistory);

  /// History of frames decoded per update.
  UnmodifiableListView<double> get decodeFrameRateHistory => UnmodifiableListView(_decodeFrameRateHistory);

  /// History of total frames dropped.
  UnmodifiableListView<int> get framesDroppedHistory => UnmodifiableListView(_framesDroppedHistory);

  /// History of inter-frame delay in seconds.
  UnmodifiableListView<double> get interFrameDelayHistory => UnmodifiableListView(_interFrameDelayHistory);

  /// History of processing delay in seconds.
  UnmodifiableListView<double> get processingDelayHistory => UnmodifiableListView(_processingDelayHistory);

  /// The last frame rate reported, in frames per second, or 0 if no data exists.
  double get lastFramesPerSecond => framesPerSecondHistory.lastOrNull ?? 0;

  /// The last bit rate reported, in bytes per second, or 0 if no data exists.
  int get lastBitRateInBytes => max(((bitrateHistory.lastOrNull ?? 0) * bytesPerMegabit).toInt(), 0);

  /// Process stats from a WebRTC report, updating the historical stat lists and notifying listeners.
  void processStats(RtcStats stats) {
    // Determine delta time, or assume 1 second update rate to avoid division by zero
    final double timeSinceLast = stats.timestampUs - (_lastTimestamp ?? 0);
    final double secondsSinceLast = timeSinceLast == 0 ? 1 : (timeSinceLast / Duration.microsecondsPerSecond);
    _lastTimestamp = stats.timestampUs;

    // Add entries for each stat
    final Map<String?, Object?> statMap = stats.values;

    _addHistory(statMap, _jitterHistory, 'jitter');
    _addHistory(statMap, _framesPerSecondHistory, 'framesPerSecond');
    _addHistory(statMap, _framesReceivedHistory, 'framesReceived');
    _addHistory(statMap, _multiFramePacketCountHistory, 'framesAssembledFromMultiplePackets');
    _addHistory(statMap, _freezeCountHistory, 'freezeCount');
    _addHistory(statMap, _nackCountHistory, 'nackCount');
    _addHistory(statMap, _pliCountHistory, 'pliCount');
    _addHistory(statMap, _firCountHistory, 'firCount');
    _addHistory(statMap, _packetsLostHistory, 'packetsLost');
    _addHistory(statMap, _keyframesDecodedHistory, 'keyFramesDecoded');
    _addHistory(statMap, _framesDroppedHistory, 'framesDropped');
    _addHistory(statMap, _packetsReceivedHistory, 'packetsReceived');
    _addHistory(statMap, _totalFreezesDurationHistory, 'totalFreezesDuration');

    // Jitter buffer delay
    _addHistory(statMap, _jitterBufferDelayHistory, 'jitterBufferDelay', (jitterBufferDelay) {
      if (!(jitterBufferDelay is double)) {
        return null;
      }

      final BigInt? emittedCount = _maybeParseBigInt(statMap['jitterBufferEmittedCount']);
      if (emittedCount == null || emittedCount == 0) {
        return null;
      }

      return (jitterBufferDelay / emittedCount.toDouble()) * Duration.millisecondsPerSecond;
    });

    // Bitrate
    _addHistory(statMap, _bitrateHistory, 'bytesReceived', (rawBytesReceived) {
      final BigInt? bytesReceived = _maybeParseBigInt(rawBytesReceived);
      if (bytesReceived == null) {
        return null;
      }

      final double megabits = (bytesReceived - _lastBytesReceived).toDouble() / bytesPerMegabit;
      _lastBytesReceived = bytesReceived;

      return megabits / secondsSinceLast;
    });

    // Decode frame rate
    _addHistory(statMap, _decodeFrameRateHistory, 'framesDecoded', (framesDecoded) {
      if (!(framesDecoded is int)) {
        return null;
      }

      final int framesDelta = framesDecoded - _lastFramesDecoded;
      _lastFramesDecoded = framesDecoded;

      return framesDelta / secondsSinceLast;
    });

    // Total inter-frame delay
    _addHistory(statMap, _interFrameDelayHistory, 'totalInterFrameDelay', (totalInterFrameDelay) {
      if (!(totalInterFrameDelay is double)) {
        return null;
      }

      final double delta = totalInterFrameDelay - _lastTotalInterFrameDelay;
      _lastTotalInterFrameDelay = totalInterFrameDelay;

      return delta;
    });

    // Total processing delay
    _addHistory(statMap, _processingDelayHistory, 'totalProcessingDelay', (totalProcessingDelay) {
      if (!(totalProcessingDelay is double)) {
        return null;
      }

      final double delta = totalProcessingDelay - _lastTotalProcessingDelay;
      _lastTotalProcessingDelay = totalProcessingDelay;

      return delta;
    });

    // Decode time
    _addHistory(statMap, _decodeTimeHistory, 'totalDecodeTime', (totalDecodeTime) {
      if (!(totalDecodeTime is double)) {
        return null;
      }

      final double delta = totalDecodeTime - _lastTotalDecodeTime;
      _lastTotalDecodeTime = totalDecodeTime;

      return delta;
    });

    notifyListeners();
  }

  /// Reset the stats, clearing all history and restoring default values.
  void reset() {
    _jitterBufferDelayHistory.clear();
    _jitterHistory.clear();
    _framesPerSecondHistory.clear();
    _framesReceivedHistory.clear();
    _multiFramePacketCountHistory.clear();
    _freezeCountHistory.clear();
    _nackCountHistory.clear();
    _pliCountHistory.clear();
    _firCountHistory.clear();
    _totalFreezesDurationHistory.clear();
    _decodeTimeHistory.clear();
    _packetsReceivedHistory.clear();
    _bitrateHistory.clear();
    _packetsLostHistory.clear();
    _keyframesDecodedHistory.clear();
    _decodeFrameRateHistory.clear();
    _framesDroppedHistory.clear();
    _interFrameDelayHistory.clear();
    _processingDelayHistory.clear();

    notifyListeners();
  }

  /// Retrieve the value of [statName] from [statMap] and add its value to a [list] if non-null, removing the first
  /// entry if the length exceeds [maxHistory].
  /// If [process] is provided, apply this function to the value before attempting to add it to the list.
  void _addHistory<T>(
    Map<String?, Object?> statMap,
    List<T> list,
    String statName, [
    T? Function(Object)? process,
  ]) {
    // Retrieve value from map
    final Object? rawValue = statMap[statName];
    if (rawValue == null) {
      return;
    }

    // Process/convert value
    final T? value;
    if (process != null) {
      value = process(rawValue);
    } else {
      if (!(rawValue is T)) {
        return;
      }

      value = rawValue as T;
    }

    if (value == null) {
      return;
    }

    // Remove old values if necessary
    while (list.length > this.maxHistory - 1) {
      list.removeAt(0);
    }

    // Add the new value
    list.add(value);
  }

  /// Helper function to parse potential BigInt data from WebRTC stats.
  /// If [data] is a string, it will be parsed as a BigInt.
  /// If [data] is a number type, the number will be converted to a BigInt.
  BigInt? _maybeParseBigInt(Object? data) {
    if (data == null) {
      return null;
    }

    if (data is num) {
      return BigInt.from(data);
    }

    if (!(data is String)) {
      return null;
    }

    try {
      return BigInt.parse(data, radix: 16);
    } catch (FormatException) {
      return null;
    }
  }
}
