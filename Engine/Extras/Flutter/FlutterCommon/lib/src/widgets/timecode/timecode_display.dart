// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:provider/provider.dart';

import '../../../localizations.dart';
import '../../../theme.dart';
import '../../../widgets.dart';
import '../../timecode/timecode.dart';
import '../../timecode/timecode_manager.dart';
import '../../timecode/timecode_source.dart';

/// Displays the current timecode.
class TimecodeDisplay extends StatefulWidget {
  const TimecodeDisplay({
    super.key,
    this.style,
    this.bShowFraction = false,
    this.errorColor = UnrealColors.highlightRed,
    this.timecodeGetter = null,
  });

  /// Text style to use. If null, the default style will be used.
  /// Regardless of the style chosen, the tabularFigures font feature will be enabled.
  final TextStyle? style;

  /// If true, show the fractional component of the frame.
  final bool bShowFraction;

  /// If not null, change text to this color when there's an error.
  final Color? errorColor;

  /// Function to retrieve the current timecode value.
  /// If null, it will be retrieved from the timecode manager.
  final Timecode Function()? timecodeGetter;

  @override
  State<TimecodeDisplay> createState() => _TimecodeDisplayState();
}

class _TimecodeDisplayState extends State<TimecodeDisplay> with TickerProviderStateMixin {
  /// Ticker used to update the timecode every frame.
  late final Ticker _ticker;

  /// Current timecode to display.
  final _timecode = ValueNotifier<Timecode>(Timecode.invalid(TimecodeInvalidReason.noData));

  /// The cached manager providing the timecode.
  late TimecodeManager _manager;

  @override
  void initState() {
    super.initState();

    _ticker = createTicker((_) => _updateTimecode());
    _ticker.start();
  }

  @override
  void dispose() {
    _timecode.dispose();
    _ticker.dispose();
    super.dispose();
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();

    _manager = Provider.of<TimecodeManager>(context);
    _updateTimecode();
  }

  @override
  Widget build(BuildContext context) {
    final TextStyle baseStyle = widget.style ?? DefaultTextStyle.of(context).style;
    final TextStyle tabularStyle = baseStyle.copyWith(
      fontFeatures: (baseStyle.fontFeatures ?? const []) + const [FontFeature.tabularFigures()],
    );

    return ValueListenableBuilder(
      valueListenable: _timecode,
      builder: (_, timecode, __) {
        if (timecode.bIsValid) {
          // Show the timecode
          return Text(
            widget.bShowFraction ? timecode.toStringWithFraction() : timecode.toString(),
            style: tabularStyle,
          );
        }

        // Show the localized error message
        final localization = EpicCommonLocalizations.of(context)!;
        final String reasonString = switch (timecode.invalidReason) {
          TimecodeInvalidReason.badData => localization.timecodeInvalidBadData,
          TimecodeInvalidReason.sourceSpecific =>
            timecode.getSourceSpecificInvalidMessage?.call(context) ?? localization.timecodeInvalidBadData,
          _ => localization.timecodeInvalidNoData,
        };

        return Text(
          reasonString.toUpperCase(),
          style: widget.errorColor != null ? tabularStyle.copyWith(color: widget.errorColor) : tabularStyle,
        );
      },
    );
  }

  /// Update the timecode and flag that its new value should be displayed.
  void _updateTimecode() {
    setState(() {
      _timecode.value = widget.timecodeGetter?.call() ?? _manager.timecode;
    });
  }
}

/// Displays an icon indicating a timecode source.
class TimecodeSourceIcon extends StatelessWidget {
  const TimecodeSourceIcon(
    this.source, {
    super.key,
    this.size,
    this.color,
  });

  /// The source to indicate.
  final TimecodeSource source;

  /// If provided, use this size for the icon. Otherwise, use the default text size for the context.
  final double? size;

  /// The color of the icon.
  final Color? color;

  @override
  Widget build(BuildContext context) {
    return AssetIcon(
      size: size,
      color: color,
      path: source.iconPath,
    );
  }
}

/// Displays an icon indicating the timecode manager's active source.
class TimecodeManagerSourceIcon extends StatelessWidget {
  const TimecodeManagerSourceIcon({
    super.key,
    this.size,
    this.color,
  });

  /// If provided, use this size for the icon. Otherwise, use the default text size for the context.
  final double? size;

  /// The color of the icon.
  final Color? color;

  @override
  Widget build(BuildContext context) {
    final manager = Provider.of<TimecodeManager>(context);
    return ValueListenableBuilder(
      valueListenable: manager.activeSource,
      builder: (context, source, _) {
        return TimecodeSourceIcon(
          source,
          size: size,
          color: color,
        );
      },
    );
  }
}
