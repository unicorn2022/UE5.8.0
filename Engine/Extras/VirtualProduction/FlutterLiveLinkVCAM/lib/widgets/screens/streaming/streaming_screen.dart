// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/theme.dart';
import 'package:epic_common/utilities/string_utils.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../../ar/api/ar_api.g.dart';
import '../../../models/net/connection_enum.dart';
import '../../../models/net/engine_connection_manager.dart';
import '../../../models/settings/vcam_settings.dart';
import '../../../webrtc/widget/touch_handler.dart';
import '../../../webrtc/widget/video_view.dart';
import '../../elements/settings_button.dart';
import '../../elements/webrtc_stat_history_view.dart';
import '../connect/connect_screen.dart';
import '../connect/reconnect_screen.dart';

/// Screen containing the WebRTC stream. Shown after successfully connecting to an instance of UE.
class StreamingScreen extends StatefulWidget {
  const StreamingScreen({super.key});

  static const route = '/streaming';

  @override
  State<StreamingScreen> createState() => _StreamingScreenState();
}

class _StreamingScreenState extends State<StreamingScreen> with WidgetsBindingObserver {
  /// The WebRTC client used to communicate with the engine.
  /// This should only be null before the first call to [didChangeDependencies].
  EngineConnectionManager? _connectionManager;

  /// Whether to show the [_arWarning].
  final _shouldShowArWarning = ValueNotifier<bool>(false);

  /// The AR availability warning to show.
  final _arWarning = ValueNotifier<Widget?>(null);

  /// The aspect ratio of the video.
  double get _aspectRatio => _connectionManager?.videoStreamSize?.aspectRatio ?? 16 / 9;

  @override
  void initState() {
    super.initState();

    WidgetsBinding.instance.addObserver(this);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _connectionManager?.videoController.size.removeListener(_onStreamSizeChanged);
    _connectionManager?.connectionState.removeListener(_onConnectionStateChanged);
    _connectionManager?.arManager?.trackingState.removeListener(_onArStateChanged);
    _connectionManager?.arManager?.hasTransformControl.removeListener(_onArStateChanged);

    _shouldShowArWarning.dispose();
    _arWarning.dispose();

    super.dispose();
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();

    _connectionManager?.videoController.size.removeListener(_onStreamSizeChanged);
    _connectionManager?.connectionState.removeListener(_onConnectionStateChanged);
    _connectionManager?.arManager?.trackingState.removeListener(_onArStateChanged);
    _connectionManager?.arManager?.hasTransformControl.removeListener(_onArStateChanged);

    _connectionManager = Provider.of<EngineConnectionManager>(context, listen: false);
    if (_connectionManager!.connectionState.value != EngineConnectionState.connected) {
      return;
    }

    _connectionManager!.videoController.size.addListener(_onStreamSizeChanged);
    _connectionManager!.connectionState.addListener(_onConnectionStateChanged);
    _connectionManager!.arManager!.trackingState.addListener(_onArStateChanged);
    _connectionManager!.arManager!.hasTransformControl.addListener(_onArStateChanged);
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    switch (state) {
      case AppLifecycleState.paused:
        _connectionManager!.disconnect();
        break;

      default:
        break;
    }
  }

  @override
  Widget build(BuildContext context) {
    final settings = Provider.of<VCamSettings>(context);

    return Container(
      color: UnrealColors.black,
      child: SafeArea(
        right: false,
        top: false,
        bottom: false,
        child: Scaffold(
          resizeToAvoidBottomInset: false,
          backgroundColor: Colors.transparent,
          body: MediaQuery.removePadding(
            context: context,
            child: Stack(
              clipBehavior: Clip.none,
              children: [
                PreferenceBuilder(
                  preference: settings.bShowInfoBar,
                  builder: (context, bool bShowInfoBar) => Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      if (bShowInfoBar)
                        _StreamInfoBar(
                          key: const Key('InfoBar'),
                        ),
                      Expanded(
                        key: const Key('WebRTCStream'),
                        child: ValueListenableBuilder(
                          valueListenable: _connectionManager!.connectionState,
                          builder: (context, connectionState, _) => connectionState == EngineConnectionState.connected
                              ? Stack(
                                  clipBehavior: Clip.none,
                                  fit: StackFit.expand,
                                  children: [
                                    RtcTouchHandler(
                                      client: _connectionManager!.webRtc,
                                      builder: (context, boundsKey) => Center(
                                        child: AspectRatio(
                                          aspectRatio: _aspectRatio,
                                          child: RtcVideoView(
                                            key: boundsKey,
                                            _connectionManager!.videoController,
                                          ),
                                        ),
                                      ),
                                    ),
                                    if (_connectionManager?.arManager != null)
                                      Align(
                                        alignment: Alignment.center,
                                        child: ValueListenableBuilder(
                                          valueListenable: _shouldShowArWarning,
                                          builder: (context, shouldShowArWarning, _) => AnimatedOpacity(
                                            key: const Key('ArUnavailableMessage'),
                                            duration: const Duration(milliseconds: 300),
                                            opacity: shouldShowArWarning ? 1 : 0,
                                            child: ValueListenableBuilder(
                                              valueListenable: _arWarning,
                                              builder: (context, arWarning, _) => arWarning ?? const SizedBox(),
                                            ),
                                          ),
                                        ),
                                      ),
                                    PreferenceBuilder(
                                      preference: settings.bShowPixelStreamingStats,
                                      builder: (context, bShowStats) => bShowStats
                                          ? WebRtcStatHistoryView(_connectionManager!.webRtc.videoStreamStats)
                                          : const SizedBox(),
                                    ),
                                  ],
                                )
                              : const Center(
                                  child: SizedBox.square(
                                    dimension: 100,
                                    child: CircularProgressIndicator(strokeWidth: 6),
                                  ),
                                ),
                        ),
                      ),
                    ],
                  ),
                ),
                const Align(
                  alignment: Alignment.topRight,
                  child: Padding(
                    padding: EdgeInsets.only(right: 24),
                    child: SettingsButton(),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  /// Called when the state of the WebRTC connection changes.
  void _onConnectionStateChanged() {
    if (_connectionManager!.connectionState.value != EngineConnectionState.connected) {
      // Clear navigation stack and return to connect screen
      Navigator.of(context).pushNamedAndRemoveUntil(
        _connectionManager!.bWasDisconnectedUnexpectedly ? ReconnectScreen.route : ConnectScreen.route,
        (route) => false,
      );
    }
  }

  /// Called when the size of the WebRTC video stream changes.
  void _onStreamSizeChanged() {
    if (!mounted) {
      return;
    }

    // Immediately rebuild the widget to accommodate the new aspect ratio
    setState(() {});
  }

  /// Called when the availability of AR tracking changes.
  void _onArStateChanged() {
    final arManager = _connectionManager?.arManager;

    // AR control is held by another device
    if (arManager?.hasTransformControl.value != true) {
      _arWarning.value = _ArWarning(
        title: AppLocalizations.of(context)!.arNotControlledOverlayTitle,
        message: AppLocalizations.of(context)!.arNotControlledOverlayMessage,
        bCanBeDismissed: true,
      );
      _shouldShowArWarning.value = true;
      return;
    }

    // AR tracking is failing
    if (arManager?.trackingState.value != ArTrackingState.normal) {
      _arWarning.value = _ArWarning(
        title: AppLocalizations.of(context)!.arUnavailableOverlayTitle,
        message: AppLocalizations.of(context)!.arUnavailableOverlayMessage,
      );
      _shouldShowArWarning.value = true;
      return;
    }

    _shouldShowArWarning.value = false;
  }
}

/// Bar that displays information about the stream.
class _StreamInfoBar extends StatelessWidget {
  const _StreamInfoBar({super.key});

  @override
  Widget build(BuildContext context) {
    final connection = Provider.of<EngineConnectionManager>(context);

    final textStyle = TextStyle(
      fontFamily: 'Droid Sans Mono',
      fontSize: 14,
      height: 1.2,
      fontFeatures: [
        FontFeature.tabularFigures(),
      ],
    );

    final separator = const Padding(
      padding: EdgeInsets.symmetric(horizontal: 8),
      child: Text('•'),
    );

    final stats = connection.webRtc.videoStreamStats;

    return Container(
      color: Theme.of(context).colorScheme.background,
      height: 48,
      padding: EdgeInsets.only(left: 16, right: 72),
      child: DefaultTextStyle(
        style: textStyle,
        child: Center(
          child: SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                const TimecodeManagerSourceIcon(size: 16),
                const SizedBox(width: 4),
                const TimecodeDisplay(errorColor: null),
                separator,
                ValueListenableBuilder(
                  valueListenable: connection.webRtc.resolution,
                  builder: (context, resolution, _) {
                    resolution = resolution ?? Size.zero;
                    return Text('${resolution.width.toInt()}x${resolution.height.toInt()}');
                  },
                ),
                separator,
                ListenableBuilder(
                  listenable: connection.webRtc.videoStreamStats,
                  builder: (context, _) => Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text('${byteCountToString(stats.lastBitRateInBytes)}'),
                      separator,
                      Text('${stats.lastFramesPerSecond.toStringAsFixed(1)} fps'),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Widget containing the message shown when AR tracking is unavailable.
class _ArWarning extends StatefulWidget {
  const _ArWarning({
    required this.title,
    required this.message,
    this.bCanBeDismissed = false,
  });

  final String title;
  final String message;
  final bool bCanBeDismissed;

  @override
  State<StatefulWidget> createState() => _ArWarningState();
}

class _ArWarningState extends State<_ArWarning> {
  bool _bIsDismissed = false;

  @override
  Widget build(BuildContext context) {
    if (_bIsDismissed) {
      return const SizedBox();
    }

    final contents = Container(
      width: 400,
      padding: const EdgeInsets.symmetric(vertical: 20, horizontal: 16),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(12),
        color: Colors.black54,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              AssetIcon(path: 'packages/epic_common/assets/icons/alert_triangle_large.svg', size: 24),
              const SizedBox(width: 8),
              Text(
                widget.title,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineSmall!.copyWith(
                      color: Colors.white,
                      fontWeight: FontWeight.bold,
                    ),
              ),
            ],
          ),
          Padding(
            padding: const EdgeInsets.only(top: 12),
            child: Text(
              widget.message,
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.headlineSmall!.copyWith(
                    color: Colors.white,
                  ),
            ),
          ),
          if (widget.bCanBeDismissed)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: EpicLozengeButton(
                label: AppLocalizations.of(context)!.arWarningDismissLabel,
                onPressed: () {
                  setState(() {
                    _bIsDismissed = true;
                  });
                },
              ),
            ),
        ],
      ),
    );

    if (widget.bCanBeDismissed) {
      return contents;
    }

    // Don't block input if this can't be dismissed
    return IgnorePointer(child: contents);
  }
}
