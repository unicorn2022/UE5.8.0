// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:math';

import 'package:epic_common/timecode.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart' hide Matrix4;
import 'package:logging/logging.dart';
import 'package:vector_math/vector_math.dart';

import '../../ar/api/ar_api.g.dart';
import '../../ar/api/session_api.dart';
import '../../ar/native/session.dart';
import '../../ar/util/ar_extension.dart';
import '../../webrtc/data/pixel_streaming_to_client_message.dart';
import '../../webrtc/data/pixel_streaming_to_streamer_message.dart';
import '../settings/vcam_settings.dart';
import 'webrtc_client.dart';

final _log = Logger('ArManager');

/// Maintains an AR session and passes tracking data on to Unreal Engine.
class UnrealArManager {
  UnrealArManager({
    required this.appSettings,
    required this.webRtc,
    required this.timecodeManager,
  }) {
    _subscriptions.addAll([
      _arSession.frameEvent.listen(_onFrame),
      _arSession.stateChangedEvent.listen(_onTrackingStateChanged),
      webRtc.listenToDataChannel(
        PixelStreamingToClientMessageKind.command,
        _onRemoteCommand,
      ),
    ]);

    requestTransformControl();
  }

  /// WebRTC client used to send messages.
  final UnrealWebRtcClient webRtc;

  /// Manager that provides the current timecode.
  final TimecodeManager timecodeManager;

  /// Common app settings that affect the AR manager.
  final VCamSettings appSettings;

  /// AR session used to get tracking data.
  final ArSession _arSession = ArSession();

  /// List of subscriptions to cancel on dispose.
  final List<StreamSubscription> _subscriptions = [];

  /// The current state of AR tracking. When unavailable, frames won't be generated.
  final _trackingState = ValueNotifier(ArTrackingState.unavailable);

  /// Whether the app has control of the UE streamer's transform.
  final _hasTransformControl = ValueNotifier(true);

  /// Random number generator for skipping updates in debug mode.
  final _skipRandom = Random();

  /// The time when we sent the last AR tracking update to the editor.
  DateTime _lastUpdateTime = DateTime.now();

  /// The current state of AR tracking. When unavailable, frames won't be generated.
  ValueListenable<ArTrackingState> get trackingState => _trackingState;

  /// Whether the app has control of the UE streamer's transform.
  ValueListenable<bool> get hasTransformControl => _hasTransformControl;

  /// Before creating an instance, call this to ensure that AR features are ready to use on the device.
  /// If the result is not [ArAvailability.available], then an ArManager can't be created.
  static Future<ArAvailability> initialize() async {
    return ArSessionApi.instance.host.initialize();
  }

  /// Start running/resume AR tracking.
  /// Returns true if the AR session successfully started/resumed.
  Future<bool> run() async {
    if (!_arSession.bInitialized) {
      try {
        await _arSession.initialize();
      } on PlatformException catch (e) {
        _log.severe('Failed to run AR session: ${e.code}\nDetails: ${e.details}');
        return false;
      }
    }

    _arSession.run();
    return true;
  }

  /// Request control of the transform.
  void requestTransformControl({bool bForce = false}) {
    webRtc.sendOnDataChannel(
      PixelStreamingToStreamerMessageKind.command,
      StreamerCommandMessage(command: {
        'VCamRequestTransformControl': {
          'Force': bForce,
        },
      }),
    );
  }

  /// Pause AR tracking.
  void pause() {
    if (_arSession.bInitialized) {
      _arSession.pause();
    }
  }

  /// Permanently stop tracking AR data and dispose of related resources.
  void dispose() {
    for (final StreamSubscription subscription in _subscriptions) {
      subscription.cancel();
    }

    if (_arSession.bInitialized) {
      _arSession.dispose();
    }
  }

  /// Called when an AR frame is received.
  void _onFrame(ArFrame event) {
    if (!_hasTransformControl.value) {
      // Don't send transform updates if we don't have control
      return;
    }

    // Debug: Randomly skip some updates
    final skipChance = appSettings.arUpdateSkipChance.getValue();
    if (skipChance > 0 && _skipRandom.nextInt(100) < skipChance) {
      return;
    }

    final now = DateTime.now();

    // Debug: Don't send updates if we would surpass the max AR update rate
    final maxArUpdatesPerSecond = appSettings.maxArUpdatesPerSecond.getValue();
    if (maxArUpdatesPerSecond > 0) {
      final Duration timeSinceLastUpdate = now.difference(_lastUpdateTime);

      if (timeSinceLastUpdate < Duration(microseconds: Duration.microsecondsPerSecond ~/ maxArUpdatesPerSecond)) {
        return;
      }
    }

    _lastUpdateTime = now;

    final transform = event.cameraTransform;

    // Convert the the transform to UE space
    final rawRotation = Quaternion.fromRotation(transform.getRotation());
    final ueRotation = Quaternion(-rawRotation.z, rawRotation.x, rawRotation.y, -rawRotation.w);
    final ueTransform = Matrix4.compose(Vector3.zero(), ueRotation, Vector3.all(1));

    const double ueUnitScale = 100; // Scale of UE units to real-world meters
    final Vector3 translation = transform.getTranslation();
    ueTransform.setTranslation(Vector3(
      -translation.z * ueUnitScale,
      translation.x * ueUnitScale,
      translation.y * ueUnitScale,
    ));

    webRtc.sendOnDataChannel(
      PixelStreamingToStreamerMessageKind.transform,
      StreamerTransformMessage(
        transform: ueTransform,
        timestamp: timecodeManager.timecode.toSeconds(),
      ),
    );
  }

  /// Called when the AR tracking state changes.
  void _onTrackingStateChanged(ArTrackingState state) {
    _log.info('AR tracking state changed: $state');
    _trackingState.value = state;
  }

  /// Called when a command is received from the WebRTC streamer.
  void _onRemoteCommand(dynamic message) {
    if (!(message is ClientCommandMessage)) {
      return;
    }

    switch (message.json['command']) {
      case 'VCamGrantTransformControl':
        _hasTransformControl.value = message.json['hasControl'] == true;
        break;
    }
  }
}
