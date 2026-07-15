// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:collection';
import 'dart:io';

import 'package:epic_common/timecode.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:logging/logging.dart';
import 'package:provider/provider.dart';

import '../../ar/api/ar_api.g.dart';
import '../../util/net_utilities.dart';
import '../../webrtc/native/media_stream_track.dart';
import '../../webrtc/native/video_view_controller.dart';
import '../../webrtc/util/gamepad_handler.dart';
import '../settings/vcam_settings.dart';
import 'ar_manager.dart';
import 'connection_enum.dart';
import 'webrtc_client.dart';

final _log = Logger('EngineConnectionManager');

/// Combines the state of all components of our connection to Unreal Engine into a clean interface.
/// This allows us to maintain a single observable connection state rather than listening to the individual pieces in
/// widget code.
class EngineConnectionManager {
  EngineConnectionManager(this.context)
      : webRtc = UnrealWebRtcClient(),
        videoController = RtcVideoViewController() {
    _gamepadHandler = RtcGamepadHandler(client: webRtc);
    webRtc.connectionState.addListener(_onRtcConnectionStateChanged);
    webRtc.videoTrack.listen(_onRtcVideoTrack);
    videoController.size.addListener(_onRtcVideoFrameSizeChanged);
  }

  /// Max time to wait for a connection step to complete before bailing out.
  static final _connectStepTimeout = Duration(seconds: 5);

  /// The context in which this was built.
  final BuildContext context;

  /// The WebRTC client used to communicate with Unreal Engine.
  final UnrealWebRtcClient webRtc;

  /// The controller for the video view.
  late final RtcVideoViewController videoController;

  /// The handler for collecting and sending gamepad input over the WebRTC data stream.
  late final RtcGamepadHandler _gamepadHandler;

  /// The current handler for collecting and sending AR data over the WebRTC data stream.
  UnrealArManager? _arManager;

  /// The current size of the streamed RTC video, or null if no size has been received in this connection session.
  Size? _videoStreamSize;

  /// Value notifier for the state of the connection to the engine.
  final _connectionState = ValueNotifier<EngineConnectionState>(EngineConnectionState.disconnected);

  /// Future that will return when the current connection attempt completes.
  Future<EngineConnectionResult>? _pendingConnectionAttempt;

  /// Completes when the video view track has been assigned for the first time since connecting.
  var _videoTrack = Completer<RtcMediaStreamTrack>();

  /// Whether the current connection attempt was cancelled.
  bool _bIsPendingConnectionCancelled = false;

  /// Whether the last disconnect was unexpected.
  bool _bWasDisconnectedUnexpectedly = false;

  /// The connection data of the last engine we connected to in this session.
  EngineConnectionData? _lastConnection;

  /// The streamer we last used in this session.
  String? _lastStreamer;

  /// If true, this is in the process of disconnecting already.
  /// This lets us ignore further calls to [disconnect] triggered by the disconnect process without updating the
  /// [connectionState], which may trigger events on external listeners and lead to an inconsistent state.
  bool _bIsDisconnecting = false;

  /// Whether the current connection is in demo mode, meaning no actual Unreal Engine instance is connected.
  bool _bIsInDemoMode = false;

  /// The current handler for collecting and sending AR data over the WebRTC data stream.
  UnrealArManager? get arManager => _arManager;

  /// The current size of the streamed RTC video, or null if no size has been received since connecting.
  Size? get videoStreamSize => _videoStreamSize;

  /// List of streamers available on the current connection.
  UnmodifiableListView<String> get streamers => webRtc.streamers;

  /// Whether the last disconnect was unexpected.
  bool get bWasDisconnectedUnexpectedly => _bWasDisconnectedUnexpectedly;

  /// The connection data of the last engine we connected to in this session.
  EngineConnectionData? get lastConnection => _lastConnection;

  /// The streamer we last used in this session.
  String? get lastStreamer => _lastStreamer;

  /// The current state of the connection to the engine.
  ValueListenable<EngineConnectionState> get connectionState => _connectionState;

  /// Disconnect and dispose all data.
  void dispose() {
    disconnect();
    videoController.dispose();
    _gamepadHandler.dispose();
    webRtc.dispose();
  }

  /// Try to connect with the given connection data. Returns a future which will provide the result of the attempt.
  /// If the result is [EngineConnectionResult.needsStreamer], call this again with a [streamer] ID from the options in
  /// [streamers] to resume the connection process.
  /// If the connection fails, this will retry until [retryDuration] has elapsed.
  Future<EngineConnectionResult> connect(
    EngineConnectionData connectionData, {
    String? streamer,
    Duration retryDuration = const Duration(seconds: 10),
  }) async {
    if (_pendingConnectionAttempt != null) {
      if (!_bIsPendingConnectionCancelled) {
        _log.warning('Tried to connect while an uncancelled connection attempt was in progress. New attempt ignored');
        return EngineConnectionResult.alreadyOpen;
      }

      // Wait for cancelled attempt to finish so we don't try to open the same socket twice
      await _pendingConnectionAttempt;
    }

    final String serverName = '${connectionData.pixelStreamingAddress.address}:${connectionData.pixelStreamingPort}'
        ' (${streamer ?? 'default'})';

    _log.info('Connecting to engine at $serverName');

    _bIsPendingConnectionCancelled = false;
    EngineConnectionResult result = EngineConnectionResult.genericFailure;
    int attempts = 0;
    DateTime startTime = DateTime.now();

    while (DateTime.now().difference(startTime) < retryDuration) {
      ++attempts;
      _log.info('Connection attempt #$attempts');

      try {
        _pendingConnectionAttempt = _internalConnect(connectionData, streamer);
        result = await _pendingConnectionAttempt!;
      } catch (error, stack) {
        _log.warning('Connection attempt threw exception: $error\n$stack');
        result = EngineConnectionResult.genericFailure;
      }

      if (result == EngineConnectionResult.needsStreamer || result == EngineConnectionResult.success) {
        // Either needs user input or succeeded, so don't try again
        break;
      }

      // If we reach this point, the connection failed, so disconnect. Clear the stored attempt first so disconnect
      // is handled immediately instead of setting _bIsPendingConnectionCancelled.
      _pendingConnectionAttempt = null;
      disconnect();

      if (_bIsPendingConnectionCancelled) {
        // If the user cancelled, don't try again
        break;
      }

      // Wait before retrying so we don't thrash the network with quick failures
      await Future.delayed(Duration(seconds: 1));
    }

    _pendingConnectionAttempt = null;

    if (result == EngineConnectionResult.needsStreamer) {
      _log.info('Multiple streamers. Waiting for user to select one.');
      _connectionState.value = EngineConnectionState.waitingForStreamer;
      return result;
    }

    if (result != EngineConnectionResult.success) {
      _log.info(
        'Gave up on trying to connect to $serverName after ${DateTime.now().difference(startTime).inSeconds} seconds '
        'with result "${result.name}"',
      );
      return result;
    }

    _log.info('Connection to $serverName successful');

    _lastConnection = connectionData;
    _lastStreamer = streamer;

    final settings = Provider.of<VCamSettings>(context, listen: false);
    settings.lastConnection.setValue(connectionData);

    _connectionState.value = EngineConnectionState.connected;
    return EngineConnectionResult.success;
  }

  /// Reconnect with the last successful connection settings from this session.
  /// If there was no previous successful connection this session, this will immediately complete with a generic failure.
  /// If [streamer] is provided, use it as the streamer instead of the last successful one.
  Future<EngineConnectionResult> reconnect({String? streamer}) {
    if (_lastConnection == null) {
      return Future.value(EngineConnectionResult.genericFailure);
    }

    return connect(_lastConnection!, streamer: streamer ?? _lastStreamer);
  }

  /// Disconnect from the currently connected engine. Ignored if the engine is already disconnected/disconnecting.
  /// [bWasDisconnectedUnexpectedly] will be set to true if [bWasExpected] is false.
  void disconnect({bool bWasExpected = true}) {
    if (_bIsDisconnecting || connectionState.value == EngineConnectionState.disconnected) {
      return;
    }

    if (_pendingConnectionAttempt != null) {
      // If this was unexpected, it means we just failed the pending attempt, so the connection logic can handle this
      // on its own
      if (bWasExpected) {
        // Don't try to disconnect here since we may violate assumptions in connection steps. Instead, flag that the
        // user cancelled and let the connect method handle it when the attempt completes.
        webRtc.cancelPendingConnection();
        _bIsPendingConnectionCancelled = true;
      }

      return;
    }

    _log.info('Disconnected (bWasExpected: $bWasExpected)');

    _bWasDisconnectedUnexpectedly = !bWasExpected;
    _bIsDisconnecting = true;

    videoController.clear();
    webRtc.disconnect();

    _gamepadHandler.stop();

    _arManager?.dispose();
    _arManager = null;

    _videoStreamSize = null;
    _videoTrack = Completer();

    _bIsDisconnecting = false;
    _connectionState.value = EngineConnectionState.disconnected;

    // Turn system UI back on
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.manual, overlays: SystemUiOverlay.values);
  }

  /// Connect to the engine specified by [connectionData].
  Future<EngineConnectionResult> _internalConnect(EngineConnectionData connectionData, [String? streamer]) async {
    if (connectionState.value != EngineConnectionState.disconnected &&
        connectionState.value != EngineConnectionState.waitingForStreamer) {
      return EngineConnectionResult.alreadyOpen;
    }

    _bIsInDemoMode = connectionData.bIsDemo;

    _connectionState.value = EngineConnectionState.connecting;

    // The video resolution we want to display
    var requestedSize = Size.zero;

    // List of asynchronous steps to perform in sequence, checking for cancellation/timeout between each
    // Each entry should be a tuple of (description, timeout, function), where the function is async and returns either
    // void or a failing EngineConnectionResult result, and the timeout specifies how long to wait before giving up (or
    // null for no timeout)
    final List<(String, Duration?, Future<EngineConnectionResult> Function())> asyncSteps = [
      (
        'Connect to Pixel Streaming',
        null, // This step has its own internal timeout
        () async {
          if (_bIsInDemoMode) {
            return EngineConnectionResult.success;
          }

          switch (webRtc.state) {
            case EngineConnectionState.waitingForStreamer:
              if (streamer == null) {
                return EngineConnectionResult.needsStreamer;
              }

              webRtc.subscribeToStreamer(streamer);
              return EngineConnectionResult.success;

            case EngineConnectionState.connected:
            case EngineConnectionState.connecting:
              // Already connected or connecting, so we're resuming a connection with a selected streamer and can skip
              // this step
              return EngineConnectionResult.success;

            default:
              break;
          }

          // Try to connect to the engine
          final result = await webRtc.connect(
            connectionData.pixelStreamingAddress,
            connectionData.pixelStreamingPort,
          );

          // If we need a streamer ID and already have one, send it and keep going
          if (result == EngineConnectionResult.needsStreamer && streamer != null) {
            webRtc.subscribeToStreamer(streamer);
            return EngineConnectionResult.success;
          }

          return result;
        },
      ),
      (
        'Wait for data channel',
        _connectStepTimeout,
        () async {
          if (!_bIsInDemoMode) {
            await webRtc.hasDataChannel;
          }

          return EngineConnectionResult.success;
        }
      ),
      (
        'Initialize video controller',
        _connectStepTimeout,
        () async {
          if (!_bIsInDemoMode) {
            await videoController.initialize();
          }

          return EngineConnectionResult.success;
        }
      ),
      (
        'Wait for video track',
        _connectStepTimeout,
        () async {
          if (!_bIsInDemoMode) {
            videoController.track = await _videoTrack.future;
            _videoStreamSize = videoController.size.value;
          }

          return EngineConnectionResult.success;
        }
      ),
      (
        'Change to full-screen UI and send resolution',
        _connectStepTimeout,
        () async {
          final systemUiChanged = Completer();

          if (Platform.isAndroid) {
            // On Android, changing to full-screen is asynchronous, but we don't want to send the resolution until it
            // completes (as reported by this callback).
            SystemChrome.setSystemUIChangeCallback((bool bAreSystemOverlaysVisible) async {
              if (!bAreSystemOverlaysVisible) {
                systemUiChanged.complete();
              }
            });
          }

          // Switch to full-screen view without OS UI
          SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);

          if (Platform.isAndroid) {
            // Just in case some Android variant doesn't give us a callback as expected. It should normally only take a
            // few frames to switch.
            await systemUiChanged.future.timeout(const Duration(milliseconds: 500), onTimeout: () {
              _log.warning('Switch to fullscreen took longer than expected');
            });

            SystemChrome.setSystemUIChangeCallback(null);
          }

          // Send the screen resolution now that the UI is removed.
          // Subtract the left padding from the screen size, which is where the camera notch padding sits.
          requestedSize = WidgetsBinding.instance.platformDispatcher.views.first.physicalSize +
              Offset(-MediaQuery.viewPaddingOf(context).left * MediaQuery.of(context).devicePixelRatio, 0);

          webRtc.setResolution(requestedSize);

          return EngineConnectionResult.success;
        }
      ),
      (
        'Wait for valid first frame',
        _connectStepTimeout,
        () async {
          if (_bIsInDemoMode) {
            return EngineConnectionResult.success;
          }

          while (!_bIsPendingConnectionCancelled) {
            // If we have a video frame ready to display at a valid size, we're done waiting
            if (videoController.hasFirstFrame.value && videoController.size.value != Size.zero) {
              break;
            }

            if (!videoController.hasFirstFrame.value) {
              _log.info('Waiting for frame...');
            } else if (videoController.size.value == Size.zero) {
              _log.info('Got a frame at ${videoController.size.value}, waiting for non-zero...');
            }

            // Clear the stored frame and the state of hasFirstFrame
            videoController.clear();

            // Wait until a new frame arrives
            final firstFrameReceived = Completer();
            void checkHasFirstFrame() {
              if (videoController.hasFirstFrame.value) {
                firstFrameReceived.complete();
              }
            }

            videoController.hasFirstFrame.addListener(checkHasFirstFrame);
            await firstFrameReceived.future;
            videoController.hasFirstFrame.removeListener(checkHasFirstFrame);
          }

          return EngineConnectionResult.success;
        }
      ),
      (
        'Initialize AR',
        null, // This step depends on user input, so don't time out
        () async {
          final bool bSuccessful = await _initAr();
          return bSuccessful ? EngineConnectionResult.success : EngineConnectionResult.arFailure;
        }
      ),
    ];

    for (final step in asyncSteps) {
      final (stepName, timeout, doStep) = step;
      _log.info('Connection step: $stepName');

      Future<EngineConnectionResult> stepFuture = doStep();

      // Apply timeout if relevant
      if (timeout != null) {
        stepFuture = stepFuture.timeout(
          timeout,
          onTimeout: () => EngineConnectionResult.timedOut,
        );
      }

      final EngineConnectionResult result = await stepFuture;

      // Step failed, so return its value
      if (result != EngineConnectionResult.success) {
        _log.info('Failed at connection step "$stepName" (result: ${result.name})');
        return result;
      }

      // Cancelled before step completed
      if (_bIsPendingConnectionCancelled) {
        _log.info('Cancelled at connection step "$stepName"');
        return EngineConnectionResult.cancelled;
      }
    }

    _gamepadHandler.start();

    return EngineConnectionResult.success;
  }

  /// Try to initialize AR features. Returns true if AR successfully started.
  Future<bool> _initAr() async {
    // Ensure that AR functionality is ready
    final ArAvailability arAvailability = await UnrealArManager.initialize();
    if (arAvailability != ArAvailability.available) {
      _log.severe('AR features not available: $arAvailability');

      final String? message;
      switch (arAvailability) {
        case ArAvailability.cameraNotPermitted:
          message = AppLocalizations.of(context)!.cameraPermissionMessage;
          break;

        default:
          message = null;
      }

      if (message != null) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              message,
              textAlign: TextAlign.center,
            ),
          ),
        );
      }

      return false;
    }

    try {
      _arManager = UnrealArManager(
        webRtc: webRtc,
        appSettings: Provider.of<VCamSettings>(context, listen: false),
        timecodeManager: Provider.of<TimecodeManager>(context, listen: false),
      );
    } catch (e) {
      _arManager = null;
      _log.severe(e);
      return false;
    }

    return _arManager!.run();
  }

  /// Called when the WebRTC client's connection state changes.
  void _onRtcConnectionStateChanged() {
    if (webRtc.connectionState.value == EngineConnectionState.disconnected) {
      // Call disconnect to make sure all the other components are in a disabled/disconnected state
      disconnect(bWasExpected: false);
    }
  }

  /// Called when the WebRTC client's active video track changes.
  void _onRtcVideoTrack(RtcMediaStreamTrack? track) {
    if (track == null || _videoTrack.isCompleted) {
      return;
    }

    _videoTrack.complete(track);
  }

  /// Called when the size of the rendered video's frames changes.
  void _onRtcVideoFrameSizeChanged() => _videoStreamSize = videoController.size.value;
}
