// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:io';
import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:logging/logging.dart';
import 'package:share_plus/share_plus.dart';

import '../../logging.dart';
import '../../theme.dart';
import '../../utilities/external_notifier.dart';
import '../../widgets.dart';

final _log = Logger('LogViewer');

/// Divider to display between entries in the log viewer.
const _divider = Divider(height: 5);

/// The base text style to use for log entries.
final _baseTextStyle = TextStyle(
  fontFamily: 'Droid Sans Mono',
  fontSize: 12,
  height: 1.2,
  fontFeatures: [
    FontFeature.tabularFigures(),
  ],
);

/// Text style for log entry message body text.
final _messageTextStyle = _baseTextStyle.copyWith(color: UnrealColors.gray90);

/// Text style for log entry message header text.
final _headerTextStyle = _baseTextStyle.copyWith(color: UnrealColors.gray75);

/// Text style for bolded log entry message header text.
final _headerBoldTextStyle = _headerTextStyle.copyWith(fontWeight: FontWeight.bold);

/// Automatically loads a log and displays it in a user-friendly format.
class LogFileViewer extends StatelessWidget {
  const LogFileViewer(this.logFile, {super.key});

  /// The file containing the log.
  final File logFile;

  @override
  Widget build(BuildContext context) {
    return FutureBuilder(
      future: Log.fromFile(logFile),
      builder: (_, AsyncSnapshot<Log?> snapshot) => (snapshot.hasData && snapshot.data != null)
          ? LogViewer(log: snapshot.data!)
          : const Center(
              child: SizedBox.square(
                dimension: 120,
                child: CircularProgressIndicator(
                  strokeWidth: 8,
                ),
              ),
            ),
    );
  }
}

/// Displays a log in a user-friendly format.
class LogViewer extends StatefulWidget {
  const LogViewer({super.key, required this.log});

  /// The log to display.
  final Log log;

  @override
  State<StatefulWidget> createState() => _LogViewerState();
}

class _LogViewerState extends State<LogViewer> {
  /// Vertical scroll controller
  final _scrollController = ScrollController();

  /// Notifier for when the text is changed
  final _textChangeNotifier = ExternalNotifier();

  /// Delegate used to generate child widgets in the scroll view.
  /// If null, the scroll view isn't ready to display.
  final _entrySliverChildDelegate = ValueNotifier<_LogEntrySliverChildDelegate?>(null);

  /// The last constraints passed down from the parent widget.
  BoxConstraints? _lastConstraints;

  /// The size of a log entry's inner message area when the message is empty.
  Size? _entryMessageSize;

  /// The size of a log entry widget when the message is empty.
  Size? _entrySize;

  /// Padding around the scrollable entry list.
  static const _listPadding = EdgeInsets.only(
    left: 8,
    right: EpicScrollBar.totalWidth,
    top: 2,
    bottom: 2,
  );

  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
    _textChangeNotifier.dispose();

    super.dispose();
  }

  @override
  void didUpdateWidget(covariant LogViewer oldWidget) {
    super.didUpdateWidget(oldWidget);

    if (oldWidget.log != widget.log) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        _scrollController.jumpTo(_scrollController.initialScrollOffset);
        _textChangeNotifier.notifyListeners();
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    // Flutter's built-in ListView.builder lazily constructs children in a scrollable list view so that only the
    // necessary widgets are built and displayed, but it performs poorly when children may be different heights.
    //
    // Instead, our strategy here is to build a log entry widget once offstage (i.e. not displayed) and measure both
    // its total size and inner message area size. We then create _entrySliverChildDelegate in _buildChildrenIfMeasured,
    // and this delegate will cheaply find child heights by only measuring the height of their log message text areas
    // (as opposed to laying out the entire widget tree for each child).

    return LayoutBuilder(builder: (_, BoxConstraints constraints) {
      if (constraints != _lastConstraints) {
        // Layout has changed, so we need to re-measure
        _entrySliverChildDelegate.value = null;

        _lastConstraints = constraints;
      }

      return ValueListenableBuilder(
        valueListenable: _entrySliverChildDelegate,
        builder: (_, entrySliverChildDelegate, __) => Offstage(
          offstage: entrySliverChildDelegate == null,
          child: Stack(
            clipBehavior: Clip.none,
            children: [
              EpicScrollBar(
                controller: _scrollController,
                child: ScrollConfiguration(
                  behavior: ScrollConfiguration.of(context).copyWith(scrollbars: false),
                  child: entrySliverChildDelegate != null
                      ? // Display the list of entries
                      ListView.custom(
                          childrenDelegate: entrySliverChildDelegate,
                          padding: _listPadding,
                          controller: _scrollController,
                        )
                      : // Build a dummy entry view to measure its available message width
                      ListView(
                          padding: _listPadding,
                          children: [
                            _LogEntryView(
                              entry: null,
                              onMessageAreaMeasured: _onEntryMessageAreaMeasured,
                              onMeasured: _onEntryMeasured,
                            )
                          ],
                          controller: _scrollController,
                        ),
                ),
              ),

              // Buttons to quickly scroll to start/end
              if (entrySliverChildDelegate != null)
                Padding(
                  padding: EdgeInsets.symmetric(vertical: 10),
                  child: Stack(
                    clipBehavior: Clip.none,
                    children: [
                      Align(
                        alignment: Alignment.topCenter,
                        child: _ScrollButton(
                          bScrollToStart: true,
                          scrollController: _scrollController,
                          resetNotifier: _textChangeNotifier,
                        ),
                      ),
                      Align(
                        alignment: Alignment.bottomCenter,
                        child: _ScrollButton(
                          bScrollToStart: false,
                          scrollController: _scrollController,
                          resetNotifier: _textChangeNotifier,
                        ),
                      ),
                    ],
                  ),
                ),
            ],
          ),
        ),
      );
    });
  }

  /// Called when the width of an entry's message has been measured.
  void _onEntryMessageAreaMeasured(Size size) {
    _entryMessageSize = size;
    _buildChildrenIfMeasured();
  }

  /// Called when the size of an entry has been measured.
  void _onEntryMeasured(Size size) {
    _entrySize = size;
    _buildChildrenIfMeasured();
  }

  /// If the entry has been fully measured, create the child sliver delegate and rebuild.
  void _buildChildrenIfMeasured() {
    if (_entryMessageSize == null || _entrySize == null) {
      return;
    }

    _entrySliverChildDelegate.value = _LogEntrySliverChildDelegate(
      context: context,
      entries: widget.log.entries,
      fixedEntryHeight: _entrySize!.height - _entryMessageSize!.height,
      messageWidth: _entryMessageSize!.width,
    );
  }
}

/// Displays a single entry in the log.
class _LogEntryView extends StatelessWidget {
  const _LogEntryView({
    required this.entry,
    this.onMeasured,
    this.onMessageAreaMeasured,
  });

  /// Format used to display the date in a log entry.
  static final _dateFormat = DateFormat('yyyy/MM/dd');

  /// Format used to display the time in a log entry.
  static final _timeFormat = DateFormat('HH:mm:ss.SSS');

  /// The log entry to display.
  /// If null, don't display log contents and only build the bare minimum to measure the entry's layout.
  final LogEntry? entry;

  /// Callback function when the size of the entire view has been measured.
  final Function(Size)? onMeasured;

  /// Callback function when the size of the message area has been measured.
  final Function(Size)? onMessageAreaMeasured;

  /// The color of the circle to display based on the entry's log level.
  Color get _color {
    switch (entry?.level) {
      case Level.INFO:
        return UnrealColors.highlightGreen;

      case Level.WARNING:
        return UnrealColors.highlightYellow;

      case Level.SEVERE:
        return UnrealColors.highlightRed;

      default:
        return UnrealColors.gray56;
    }
  }

  @override
  Widget build(BuildContext context) {
    Widget messageWidget = RichText(
      text: TextSpan(
        text: entry?.message,
        style: _messageTextStyle,
      ),
    );

    if (onMessageAreaMeasured != null) {
      messageWidget = Measurable(
        child: messageWidget,
        onMeasured: onMessageAreaMeasured!,
      );
    }

    Widget mainBody = Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        // Log level indicator
        Icon(
          Icons.circle,
          size: 16,
          color: _color,
        ),
        const SizedBox(width: 4),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            mainAxisSize: MainAxisSize.min,
            children: [
              // Header
              RichText(
                softWrap: false, // IMPORTANT: this is assumed to be one line in height, so it can't wrap
                overflow: TextOverflow.fade,
                text: TextSpan(
                  children: [
                    TextSpan(
                      text: ((entry != null) ? _dateFormat.format(entry!.time) : '-') + ' ',
                      style: _headerTextStyle,
                    ),
                    TextSpan(
                      text: ((entry != null) ? _timeFormat.format(entry!.time) : '-') + ' ',
                      style: _headerBoldTextStyle,
                    ),
                    TextSpan(
                      text: '[${entry?.loggerName}]',
                      style: _headerTextStyle,
                    ),
                  ],
                ),
              ),

              const SizedBox(height: 2),

              messageWidget,
            ],
          ),
        ),
      ],
    );

    if (onMeasured != null) {
      mainBody = Measurable(
        child: mainBody,
        onMeasured: onMeasured!,
      );
    }

    return RepaintBoundary(child: mainBody);
  }
}

/// A button that lets the user instantly scroll to the start or end and hides itself when not relevant.
class _ScrollButton extends StatefulWidget {
  const _ScrollButton({
    Key? key,
    required this.scrollController,
    required this.bScrollToStart,
    this.resetNotifier,
  }) : super(key: key);

  /// Controller used to scroll the controlled view.
  final ScrollController scrollController;

  /// Whether this should scroll to the start (true) or end (false) of the scroll view.
  final bool bScrollToStart;

  /// If provided, update the button's visibility when this notifier fires (e.g. scroll bounds have changed).
  final ChangeNotifier? resetNotifier;

  @override
  State<_ScrollButton> createState() => _ScrollButtonState();
}

class _ScrollButtonState extends State<_ScrollButton> {
  bool _bIsVisible = false;
  bool _bIsAnimating = false;

  @override
  void initState() {
    super.initState();
    widget.scrollController.addListener(_updateVisibility);
    widget.resetNotifier?.addListener(_updateVisibility);

    WidgetsBinding.instance.addPostFrameCallback((_) {
      _updateVisibility();
    });
  }

  @override
  void dispose() {
    super.dispose();

    widget.scrollController.removeListener(_updateVisibility);
    widget.resetNotifier?.removeListener(_updateVisibility);
  }

  @override
  void didUpdateWidget(covariant _ScrollButton oldWidget) {
    super.didUpdateWidget(oldWidget);

    oldWidget.resetNotifier?.removeListener(_updateVisibility);
    oldWidget.scrollController.removeListener(_updateVisibility);

    widget.resetNotifier?.addListener(_updateVisibility);
    widget.scrollController.addListener(_updateVisibility);
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedOpacity(
      opacity: (_bIsVisible && !_bIsAnimating) ? 1 : 0,
      duration: Duration(milliseconds: 150),
      child: FloatingActionButton(
        child: AssetIcon(
          path: widget.bScrollToStart
              ? 'packages/epic_common/assets/icons/chevron_up.svg'
              : 'packages/epic_common/assets/icons/chevron_down.svg',
          size: 32,
        ),
        onPressed: _bIsVisible ? _onPressed : null,
        heroTag: null,
      ),
    );
  }

  /// Update whether to show the button based on scroll position.
  void _updateVisibility() {
    bool bNewIsVisible = false;
    final double? scrollTarget = _getScrollTarget();
    if (scrollTarget == null) {
      bNewIsVisible = false;
    } else {
      bNewIsVisible = widget.scrollController.offset != scrollTarget;
    }

    if (bNewIsVisible != _bIsVisible) {
      setState(() {
        _bIsVisible = bNewIsVisible;
      });
    }
  }

  /// Called when the button is pressed.
  void _onPressed() {
    final double? scrollTarget = _getScrollTarget();
    if (scrollTarget != null) {
      setState(() {
        _bIsAnimating = true;
      });

      widget.scrollController
          .animateTo(
            scrollTarget,
            duration: Duration(milliseconds: 300),
            curve: Curves.easeInOutCubic,
          )
          .then((_) => setState(() {
                _bIsAnimating = false;
              }));
    }
  }

  /// Get the position to scroll to when the button is pressed.
  double? _getScrollTarget() {
    final scrollPosition = widget.scrollController.position;
    if (!scrollPosition.hasContentDimensions) {
      return null;
    }

    return widget.bScrollToStart ? scrollPosition.minScrollExtent : scrollPosition.maxScrollExtent;
  }
}

/// Delegate which takes a list of log entries and provides widgets to display each log entry.
class _LogEntrySliverChildDelegate extends SliverChildDelegate {
  _LogEntrySliverChildDelegate({
    required this.context,
    required this.entries,
    required this.fixedEntryHeight,
    required this.messageWidth,
  });

  /// The context in which this will build widgets.
  final BuildContext context;

  /// The list of entries to display.
  final List<LogEntry> entries;

  /// The fixed portion of an entry's height (i.e. the height without the message included).
  final double fixedEntryHeight;

  /// The width available for log entry messages.
  final double messageWidth;

  /// Cached bottom position of each child's widget within the overall scroll bounds.
  final List<double> _childBottomPositions = [];

  @override
  // Add extra child count to display the divider.
  int? get estimatedChildCount => entries.length * 2 - 1;

  @override
  int? findIndexByKey(Key key) {
    return (key as ValueKey<int>).value;
  }

  @override
  Widget? build(BuildContext context, int index) {
    if (index >= estimatedChildCount!) {
      return null;
    }

    final int? entryIndex = _getLogEntryIndexForChild(index);

    // Show divider between each entry
    if (entryIndex == null) {
      return _divider;
    }

    return IndexedSemantics(
      key: ValueKey<int>(entryIndex),
      index: entryIndex,
      child: _LogEntryView(
        entry: entries[entryIndex],
      ),
    );
  }

  @override
  bool shouldRebuild(covariant _LogEntrySliverChildDelegate oldDelegate) {
    return oldDelegate.context != context || oldDelegate.entries != entries;
  }

  @override
  double? estimateMaxScrollOffset(
    int firstIndex,
    int lastIndex,
    double leadingScrollOffset,
    double trailingScrollOffset,
  ) {
    return _getChildBottomPosition(estimatedChildCount!);
  }

  /// Returns the index of the log entry for the child with the given [index], or null if the child is a divider.
  int? _getLogEntryIndexForChild(int index) {
    if (index % 2 == 1) {
      return null;
    }

    return index ~/ 2;
  }

  /// Get the bottom position of the child with the given [childIndex].
  /// Uses iterative memoization to reduce cost of calculation, so calling for any given value will be cheap after the
  /// first time.
  double _getChildBottomPosition(int childIndex) {
    final textPainter = TextPainter(
      textDirection: Directionality.of(context),
    );

    for (int subIndex = _childBottomPositions.length; subIndex <= childIndex; ++subIndex) {
      final double prevChildBottom = (subIndex == 0) ? 0 : _childBottomPositions[subIndex - 1];
      final double childHeight;

      final int? entryIndex = _getLogEntryIndexForChild(subIndex);
      if (entryIndex == null) {
        childHeight = _divider.height!;
      } else {
        textPainter.text = TextSpan(
          text: entries[entryIndex].message,
          style: _messageTextStyle,
        );
        textPainter.layout(maxWidth: messageWidth);

        childHeight = textPainter.height + fixedEntryHeight;
      }

      _childBottomPositions.add(prevChildBottom + childHeight);
    }

    return _childBottomPositions[childIndex];
  }
}

/// Arguments passed to the route when a [LogViewSettingsPage] is pushed to the navigation stack.
class LogViewSettingsPageArguments {
  const LogViewSettingsPageArguments({required this.file, required this.bIsCurrent});

  /// The file to open.
  final File file;

  /// Whether this is the current log.
  final bool bIsCurrent;
}

/// Settings page that displays an individual log.
class LogViewSettingsPage extends StatefulWidget {
  const LogViewSettingsPage({super.key});

  @override
  State<LogViewSettingsPage> createState() => _LogViewSettingsPageState();
}

class _LogViewSettingsPageState extends State<LogViewSettingsPage> {
  final shareButtonKey = GlobalKey();
  @override
  Widget build(BuildContext context) {
    final arguments = ModalRoute.of(context)?.settings.arguments as LogViewSettingsPageArguments;

    return SettingsPageScaffold(
      bWrapBodyInScrollView: false,
      title: Logging.makeNameForLog(context: context, logFile: arguments.file, bIsCurrent: arguments.bIsCurrent),
      titleBarTrailing: EpicIconButton(
        key: shareButtonKey,
        iconPath: Platform.isIOS
            ? 'packages/epic_common/assets/icons/share.svg'
            : 'packages/epic_common/assets/icons/share_android.svg',
        onPressed: () => _shareLog(arguments.file),
      ),
      body: SizedBox(height: MediaQuery.of(context).size.height - 120, child: LogFileViewer(arguments.file)),
    );
  }

  /// Open a prompt to share the log's contents using the local operating system.
  void _shareLog(File log) async {
    // Need to provide this position for sharing to work on iPad
    final renderBox = shareButtonKey.currentContext?.findRenderObject() as RenderBox?;
    if (renderBox == null) {
      _log.warning('Tried to share log, but no renderBox was found');
      return;
    }

    final sharePosition = renderBox.localToGlobal(Offset.zero) & renderBox.size;
    final fileName = log.uri.pathSegments.last;

    final ShareResult result = await Share.shareXFiles(
      [XFile(log.path)],
      subject: log.uri.pathSegments.last,
      sharePositionOrigin: sharePosition,
    );

    _log.info('Shared log "$fileName" with result "${result.status.name}"');
  }
}

/// Settings page that displays a list of all application logs.
class LogListSettingsPage extends StatefulWidget {
  const LogListSettingsPage(this.title, this.route, {super.key});

  /// Title of this page.
  final String title;

  /// Route for the individual log view.
  final String route;

  @override
  State<LogListSettingsPage> createState() => _LogListSettingsPageState();
}

class _LogListSettingsPageState extends State<LogListSettingsPage> {
  /// Scroll controller for the scroll view of the logs.
  final _scrollController = ScrollController();

  /// List of files to display.
  List<File> _logFiles = [];

  @override
  void initState() {
    super.initState();

    _updateLogFileList();
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  /// Update the list of log files.
  void _updateLogFileList() async {
    _logFiles = await Logging.getAppLogFiles();
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    return SettingsPageScaffold(
      title: widget.title,
      bWrapBodyInScrollView: false,
      body: SizedBox(
        height: 240,
        child: MediaQuery.removePadding(
          removeTop: true,
          removeBottom: true,
          context: context,
          child: EpicListView(
            itemCount: _logFiles.length,
            itemBuilder: (context, index) {
              final File file = _logFiles[index];
              final bool bIsCurrent = index == 0;

              return SettingsMenuItem(
                title: Logging.makeNameForLog(context: context, logFile: file, bIsCurrent: bIsCurrent),
                iconPath: 'packages/epic_common/assets/icons/log.svg',
                onTap: () => Navigator.of(context).pushNamed(
                  widget.route,
                  arguments: LogViewSettingsPageArguments(file: file, bIsCurrent: bIsCurrent),
                ),
              );
            },
          ),
        ),
      ),
    );
  }
}
