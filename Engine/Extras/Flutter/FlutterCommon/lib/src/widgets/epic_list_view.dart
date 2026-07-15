// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

import '../../theme.dart';

typedef IndexedWidgetBuilder(BuildContext context, int index);

/// Core ListView for the app, wraps a [EpicScrollBar] + [ListView.builder].
class EpicListView extends StatefulWidget {
  /// defines extent for the children of the ListView.
  final double? itemExtent;

  /// Number of items to be rendered on the list.
  final int itemCount;

  /// Additional padding around the main scrollable area.
  final EdgeInsetsGeometry padding;

  /// Builders callback for rendering each item in the current [context] and for the given [index]
  final IndexedWidgetBuilder itemBuilder;

  /// whether to show scrollbar or not.
  final bool? bShowScrollbar;

  /// Color of the scrollbar thumb.
  final Color? scrollbarThumbColor;

  EpicListView({
    Key? key,
    required this.itemBuilder,
    required this.itemCount,
    this.itemExtent,
    this.padding = EdgeInsets.zero,
    this.bShowScrollbar,
    this.scrollbarThumbColor,
  }) : super(key: key);

  @override
  _EpicListViewState createState() => _EpicListViewState();
}

class _EpicListViewState extends State<EpicListView> {
  late ScrollController _controller = ScrollController();

  /// Whether to display the scrollbar
  bool _bScrollbarVisible = false;

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return NotificationListener<ScrollMetricsNotification>(
      onNotification: _onScrollMetricsNotification,
      child: EpicScrollBar(
        controller: _controller,
        padding: widget.padding,
        bEnabled: widget.bShowScrollbar ?? true,
        thumbColor: widget.scrollbarThumbColor,
        child: ScrollConfiguration(
          behavior: ScrollConfiguration.of(context).copyWith(scrollbars: false),
          child: ListView.builder(
            controller: _controller,
            itemExtent: widget.itemExtent,
            itemCount: widget.itemCount,
            padding: EdgeInsets.only(right: _bScrollbarVisible ? EpicScrollBar.totalWidth : 0),
            itemBuilder: (BuildContext context, int index) {
              return Padding(
                padding: EdgeInsets.only(
                  top: (index > 0) ? 2 : 0,
                ),
                child: widget.itemBuilder(context, index),
              );
            },
          ),
        ),
      ),
    );
  }

  /// If there's enough content to scroll, enable the scrollbar. Otherwise, hide it.
  bool _onScrollMetricsNotification(ScrollMetricsNotification notification) {
    if (!mounted) {
      return false;
    }

    final ScrollMetrics metrics = notification.metrics;
    if (metrics.axisDirection != AxisDirection.down) {
      return false;
    }

    final bool bNewScrollbarVisible = metrics.hasContentDimensions && metrics.minScrollExtent < metrics.maxScrollExtent;
    if (bNewScrollbarVisible != _bScrollbarVisible) {
      setState(() {
        _bScrollbarVisible = bNewScrollbarVisible;
      });
    }

    return false;
  }
}

/// Styled theme scroll bar.
class EpicScrollBar extends StatelessWidget {
  const EpicScrollBar({
    Key? key,
    required this.child,
    required this.controller,
    this.bEnabled = true,
    this.notification = defaultScrollNotificationPredicate,
    this.thumbColor,
    this.padding = EdgeInsets.zero,
  }) : super(key: key);

  static const double mainAxisMargin = 8;
  static const double crossAxisMargin = 8;
  static const double thickness = 8;
  static const double totalWidth = (mainAxisMargin * 2) + thickness;

  /// Whether styled scrollbar is enabled or not.
  final bool bEnabled;

  /// Child widget to be wrapped with scrollbar possibly be a list.
  final Widget child;

  /// Creating a binding with the [child] scroll view.
  final ScrollController controller;

  /// Color for the scrollbar handle or thumb region.
  final Color? thumbColor;

  /// Scrollbar padding.
  final EdgeInsetsGeometry padding;

  /// Tell the scrollbar which ScrollView to control and respond to as [child] could have multiple scroll views.
  final ScrollNotificationPredicate notification;

  @override
  Widget build(BuildContext context) {
    return bEnabled
        ? Padding(
            padding: padding,
            child: RawScrollbar(
              mainAxisMargin: mainAxisMargin,
              crossAxisMargin: crossAxisMargin,
              controller: controller,
              radius: Theme.of(context).scrollbarTheme.radius,
              thickness: thickness,
              trackVisibility: false,
              thumbVisibility: true,
              thumbColor: thumbColor ?? UnrealColors.gray22,
              notificationPredicate: notification,
              child: child,
            ),
          )
        : child;
  }
}
