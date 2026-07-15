import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

import '../../models/net/webrtc_stat_history.dart';
import 'line_graph.dart';

/// Displays graphs of all stats for a WebRTC video stream.
class WebRtcStatHistoryView extends StatelessWidget {
  const WebRtcStatHistoryView(this.statHistory, {super.key});

  /// The stat history to display.
  final WebRtcStatHistory statHistory;

  @override
  Widget build(BuildContext context) {
    final localizations = AppLocalizations.of(context)!;

    final graphConfigs = [
      _StatGraphConfig(
        title: localizations.statGraphTitleBitrate,
        getValues: (statHistory) => statHistory.bitrateHistory,
        defaultMaxY: 20,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleFrameRate,
        getValues: (statHistory) => statHistory.framesPerSecondHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleFramesReceived,
        getValues: (statHistory) => statHistory.framesReceivedHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleMultiPacketFrames,
        getValues: (statHistory) => statHistory.multiFramePacketCountHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleJitter,
        getValues: (statHistory) => statHistory.jitterHistory,
        defaultMaxY: 0.05,
        yTickLabelFormatter: (value) => value.toStringAsFixed(2),
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleJitterBufferDelay,
        getValues: (statHistory) => statHistory.jitterBufferDelayHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitlePacketsLost,
        getValues: (statHistory) => statHistory.packetsLostHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleNackCount,
        getValues: (statHistory) => statHistory.nackCountHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleFreezeCount,
        getValues: (statHistory) => statHistory.freezeCountHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitlePictureLossCount,
        getValues: (statHistory) => statHistory.pliCountHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleKeyframesDecoded,
        getValues: (statHistory) => statHistory.keyframesDecodedHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleDecodeFrameRate,
        getValues: (statHistory) => statHistory.decodeFrameRateHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleFramesDropped,
        getValues: (statHistory) => statHistory.framesDroppedHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleInterFrameDelay,
        getValues: (statHistory) => statHistory.interFrameDelayHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleProcessingDelay,
        getValues: (statHistory) => statHistory.processingDelayHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleFirCount,
        getValues: (statHistory) => statHistory.firCountHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleFreezeDuration,
        getValues: (statHistory) => statHistory.totalFreezesDurationHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitlePacketsReceived,
        getValues: (statHistory) => statHistory.packetsReceivedHistory,
      ),
      _StatGraphConfig(
        title: localizations.statGraphTitleDecodeTime,
        getValues: (statHistory) => statHistory.decodeTimeHistory,
      ),
    ];

    return Scrollbar(
      child: GridView.builder(
        padding: EdgeInsets.symmetric(horizontal: 64, vertical: 32),
        scrollDirection: Axis.vertical,
        gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
          maxCrossAxisExtent: 180,
          mainAxisSpacing: 16,
          crossAxisSpacing: 32,
        ),
        itemCount: graphConfigs.length,
        itemBuilder: (_, index) => _StatGraph(
          key: Key('StatGraph_$index'),
          config: graphConfigs[index],
          statHistory: statHistory,
        ),
      ),
    );
  }
}

/// Configuration for a [_StatGraph].
class _StatGraphConfig {
  const _StatGraphConfig({
    required this.title,
    required this.getValues,
    this.defaultMaxY = 10,
    this.yTickLabelFormatter,
  });

  /// The title of the graph.
  final String title;

  /// Function to retrieve the values from the stat history.
  final List<num> Function(WebRtcStatHistory) getValues;

  /// The default max Y value of the graph. This will be increased to add headroom if values pass this.
  final double defaultMaxY;

  /// Formatter for tick labels on the Y axis.
  final String Function(num)? yTickLabelFormatter;
}

/// Listens to the given stat history and redraws the graph when the stats change.
class _StatGraph extends StatelessWidget {
  const _StatGraph({
    super.key,
    required this.config,
    required this.statHistory,
  });

  /// The stat history from which to retrieve values.
  final WebRtcStatHistory statHistory;

  /// Configuration for the graph.
  final _StatGraphConfig config;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: statHistory,
      builder: (_, __) => LineGraph(
        title: config.title,
        values: config.getValues(statHistory),
        defaultMaxY: config.defaultMaxY,
        yTickLabelFormatter: config.yTickLabelFormatter,
      ),
    );
  }
}
