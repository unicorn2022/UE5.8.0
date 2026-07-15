import 'dart:math';
import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

/// A graph that displays a value over time.
class LineGraph extends StatelessWidget {
  const LineGraph({
    super.key,
    required this.title,
    required this.values,
    this.defaultMaxY = 1,
    this.maxValues = 120,
    this.yTickLabelFormatter,
  }) : assert(values.length <= maxValues);

  /// The title of the graph.
  final String title;

  /// The values to display in the graph where an even amount of time is assumed to have passed between each value.
  final List<num> values;

  /// The default max Y value of the graph. This will be increased to add headroom if values pass this.
  final double defaultMaxY;

  /// The maximum number of values to display. Graph will shift to the right if not enough values are provided.
  /// [values] must be of length <= [maxValues].
  final int maxValues;

  /// Formatter for tick labels on the Y axis.
  final String Function(num)? yTickLabelFormatter;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: Colors.black54,
        borderRadius: BorderRadius.circular(8),
      ),
      padding: EdgeInsets.symmetric(vertical: 2),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text(
            title,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.labelSmall,
          ),
          Expanded(
            child: values.isNotEmpty
                ? CustomPaint(
                    painter: _LineGraphPainter(
                      values: values,
                      defaultMaxY: defaultMaxY,
                      maxValues: maxValues,
                      yTickLabelFormatter: yTickLabelFormatter,
                      tickLabelStyle: Theme.of(context).textTheme.labelSmall!.copyWith(
                            fontSize: 8,
                            letterSpacing: 0,
                          ),
                    ),
                  )
                : Center(
                    child: Text(
                      AppLocalizations.of(context)!.statGraphNoDataLabel,
                      style: Theme.of(context).textTheme.labelLarge,
                    ),
                  ),
          ),
        ],
      ),
    );
  }
}

/// Paints the line graph's area under the title.
class _LineGraphPainter extends CustomPainter {
  const _LineGraphPainter({
    required this.values,
    required this.defaultMaxY,
    required this.maxValues,
    required this.yTickLabelFormatter,
    required this.tickLabelStyle,
  });

  /// The values to display in the graph where an even amount of time is assumed to have passed between each value.
  final List<num> values;

  /// The default max Y value of the graph. This will be increased to add headroom if values pass this.
  final double defaultMaxY;

  /// The maximum number of values to display. Graph will shift to the right if not enough values are provided;
  /// otherwise, only the last [maxValues] values will be read from [values].
  final int maxValues;

  /// Formatter for tick labels on the Y axis.
  final String Function(num)? yTickLabelFormatter;

  /// The text style to use for tick labels.
  final TextStyle tickLabelStyle;

  @override
  void paint(Canvas canvas, Size size) {
    final Rect rect = Offset.zero & size;

    canvas.clipRect(rect, doAntiAlias: false);

    // Determine the value corresponding to the highest point in the graph.
    const double minHeadroom = 1.1;
    num maxValue = values.fold(0, (maxValue, value) => max(maxValue, value));
    maxValue = max(defaultMaxY, maxValue * minHeadroom);

    // Set up functions for scaling values to Y positions
    const double yMin = 8;
    final double yMax = size.height - 6;

    final yPosForValue = (value) => ((1 - (value / maxValue)) * (yMax - yMin)) + yMin;

    const double tickAreaFraction = 0.15;
    final double tickAreaWidth = tickAreaFraction * size.width;

    if (values.isNotEmpty && maxValue != 0) {
      // Set up functions for scaling history indices to X positions
      final double lineXMin = tickAreaWidth + ((size.width - tickAreaWidth) * (1 - (values.length / maxValues)));
      final double lineXMax = size.width;

      final lineXPosForIndex = (index) => ((index / values.length) * (lineXMax - lineXMin)) + lineXMin;

      // Draw lines
      final linePaint = Paint()
        ..color = Colors.red
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1;

      final List<Offset> points = [];

      for (int valueIndex = 0; valueIndex < values.length; ++valueIndex) {
        final offset = Offset(
          lineXPosForIndex(valueIndex),
          yPosForValue(values[valueIndex]),
        );

        if (offset.dx.isNaN || offset.dy.isNaN) {
          // Just in case, avoid an assert when trying to draw this line
          continue;
        }

        points.add(offset);
      }

      canvas.drawPoints(PointMode.polygon, points, linePaint);
    }

    // Draw Y axis
    final tickPaint = Paint()..color = Colors.white;

    canvas.drawLine(
      Offset(tickAreaWidth, yMin),
      Offset(tickAreaWidth, yMax),
      tickPaint,
    );

    // Draw Y axis ticks
    const tickCount = 6;
    const tickWidth = 5;

    final tickLabelPainter = TextPainter(textDirection: TextDirection.ltr);

    for (int tickIndex = 0; tickIndex < tickCount; ++tickIndex) {
      final double tickValue = maxValue * (tickIndex / (tickCount - 1));
      final double tickY = yPosForValue(tickValue);

      canvas.drawLine(
        Offset(tickAreaWidth - tickWidth, tickY),
        Offset(tickAreaWidth, tickY),
        tickPaint,
      );

      final labelText = (yTickLabelFormatter == null) ? '${tickValue.round()}' : yTickLabelFormatter!(tickValue);

      final labelTextSpan = TextSpan(
        text: labelText,
        style: tickLabelStyle,
      );

      tickLabelPainter.text = labelTextSpan;
      tickLabelPainter.layout();
      tickLabelPainter.paint(
        canvas,
        Offset(
          tickAreaWidth - (tickWidth + tickLabelPainter.width + 1),
          tickY - tickLabelPainter.height / 2,
        ),
      );
    }
  }

  @override
  bool shouldRepaint(covariant _LineGraphPainter oldDelegate) {
    return oldDelegate.values != values ||
        oldDelegate.defaultMaxY != defaultMaxY ||
        oldDelegate.maxValues != maxValues ||
        oldDelegate.tickLabelStyle != tickLabelStyle;
  }
}
