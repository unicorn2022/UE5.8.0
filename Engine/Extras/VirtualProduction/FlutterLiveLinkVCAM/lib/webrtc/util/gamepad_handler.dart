// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:logging/logging.dart';

import '../../gamepad/api/gamepad_api.dart';
import '../../gamepad/api/gamepad_api.g.dart';
import '../../models/net/webrtc_client.dart';
import '../data/pixel_streaming_to_client_message.dart';
import '../data/pixel_streaming_to_streamer_message.dart';

final _log = Logger('RtcGamepadHandler');

/// Handles gamepad events by sending them over the WebRTC client's data channel.
class RtcGamepadHandler {
  RtcGamepadHandler({
    required this.client,
  }) {}

  // How frequently to send analog inputs to the engine
  static final Duration _analogTickInterval = const Duration(microseconds: 16667);

  /// Map from gamepad input to the ID to send over WebRTC.
  /// This includes both digital and analog inputs, so some values will overlap.
  static final Map<GamepadInput, int> _inputIds = {
    GamepadInput.faceButtonBottom: 0,
    GamepadInput.faceButtonRight: 1,
    GamepadInput.faceButtonLeft: 2,
    GamepadInput.faceButtonTop: 3,
    GamepadInput.shoulderButtonLeft: 4,
    GamepadInput.shoulderButtonRight: 5,
    GamepadInput.triggerButtonLeft: 6,
    GamepadInput.triggerButtonRight: 7,
    GamepadInput.specialButtonLeft: 8,
    GamepadInput.specialButtonRight: 9,
    GamepadInput.thumbstickLeftButton: 10,
    GamepadInput.thumbstickRightButton: 11,
    GamepadInput.dpadUp: 12,
    GamepadInput.dpadDown: 13,
    GamepadInput.dpadLeft: 14,
    GamepadInput.dpadRight: 15,
    GamepadInput.thumbstickLeftX: 1,
    GamepadInput.thumbstickLeftY: 2,
    GamepadInput.thumbstickRightX: 3,
    GamepadInput.thumbstickRightY: 4,
    GamepadInput.triggerAxisLeft: 5,
    GamepadInput.triggerAxisRight: 6,
  };

  /// The WebRTC client used to communicate input.
  final UnrealWebRtcClient client;

  /// Subscriptions to cancel when this is stopped.
  final List<StreamSubscription> _subscriptions = [];

  /// List of gamepad IDs that are waiting for an Unreal index (in the order they were reported to Unreal).
  final List<int> _gamepadIdsPendingUnrealIndex = [];

  /// List of gamepad IDs that were disconnected before receiving an Unreal index.
  final List<int> _gamepadIdsDisconnectedEarly = [];

  /// Map from native gamepad ID to Unreal controller index.
  final Map<int, int> _unrealIndexByGamepadId = {};

  /// Map from Unreal controller index to native ID.
  final Map<int, int> _gamepadIdByUnrealIndex = {};

  /// Map from Unreal controller index to set of analog inputs that have been changed since values were last sent.
  final Map<int, Set<GamepadInput>> _dirtyAxisInputs = {};

  /// Timer used to send analog movement updates.
  Timer? _analogTimer;

  /// Start sending input events via WebRTC.
  /// This will send initial values for each
  void start() {
    if (_analogTimer?.isActive == true) {
      _log.warning('Tried to start gamepad handler while it was already running');
      return;
    }

    final gamepadApi = GamepadApi.instance;

    _subscriptions.addAll([
      gamepadApi.connectedEvents.listen(_onGamepadConnected),
      gamepadApi.disconnectedEvents.listen(_onGamepadDisconnected),
      gamepadApi.inputEvents.listen(_onInputEvent),
      client.listenToDataChannel(PixelStreamingToClientMessageKind.gamepadResponse, _onGamepadResponse),
    ]);

    _sendAllControllers();

    _analogTimer?.cancel();
    _analogTimer = Timer.periodic(_analogTickInterval, (_) => _onAnalogTick());
  }

  /// Temporarily stop sending input events via WebRTC until [start] is called again.
  void stop() {
    _analogTimer?.cancel();

    _subscriptions.forEach((subscription) => subscription.cancel());

    _gamepadIdByUnrealIndex.clear();
    _unrealIndexByGamepadId.clear();
    _gamepadIdsPendingUnrealIndex.clear();
    _gamepadIdsDisconnectedEarly.clear();
  }

  /// Stop sending events and dispose of the handler's state.
  void dispose() {
    stop();

    _analogTimer?.cancel();
  }

  /// Called each time we want to sent analog inputs to the engine.
  void _onAnalogTick() {
    for (final gamepadId in _unrealIndexByGamepadId.keys) {
      _sendGamepadAxes(gamepadId);
    }
  }

  /// Called when a gamepad is connected to the app.
  void _onGamepadConnected(int gamepadId) {
    _gamepadIdsPendingUnrealIndex.add(gamepadId);
    client.sendOnDataChannel(PixelStreamingToStreamerMessageKind.gamepadConnected);
  }

  /// Called when a gamepad is disconnected from the app.
  void _onGamepadDisconnected(int gamepadId) {
    final int? unrealIndex = _unrealIndexByGamepadId[gamepadId];
    if (unrealIndex == null) {
      if (_gamepadIdsPendingUnrealIndex.contains(gamepadId)) {
        // Gamepad was disconnected before we received a response from the engine
        _gamepadIdsDisconnectedEarly.add(gamepadId);
      } else {
        _log.warning('A previously unseen gamepad with ID $gamepadId was disconnected');
      }

      return;
    }

    _unrealIndexByGamepadId.remove(gamepadId);
    _gamepadIdByUnrealIndex.remove(unrealIndex);

    client.sendOnDataChannel(
      PixelStreamingToStreamerMessageKind.gamepadDisconnected,
      StreamerGamepadDisconnectedMessage(
        controllerIndex: unrealIndex,
      ),
    );
  }

  /// Called when the WebRTC streamer responds with an ID for a connected controller.
  void _onGamepadResponse(dynamic message) {
    if (message is! ClientGamepadResponseMessage) {
      return;
    }

    if (_gamepadIdsPendingUnrealIndex.isEmpty) {
      _log.warning('Received gamepad response when there were no pending gamepads');
      return;
    }

    final int gamepadId = _gamepadIdsPendingUnrealIndex.removeAt(0);
    if (_gamepadIdsDisconnectedEarly.remove(gamepadId)) {
      // Gamepad was disconnected, so send the message and forget it
      client.sendOnDataChannel(
        PixelStreamingToStreamerMessageKind.gamepadDisconnected,
        StreamerGamepadDisconnectedMessage(
          controllerIndex: gamepadId,
        ),
      );
      return;
    }

    final int unrealIndex = message.controllerIndex;
    _unrealIndexByGamepadId[gamepadId] = unrealIndex;
    _gamepadIdByUnrealIndex[unrealIndex] = gamepadId;
    _dirtyAxisInputs[unrealIndex] = GamepadApi.instance.axes.toSet();

    _sendGamepadAxes(gamepadId);
  }

  /// Called when input is received from a gamepad.
  void _onInputEvent(GamepadInputEvent event) {
    switch (event.type) {
      case GamepadInputType.axis:
        _onAxisInputEvent(event);
        break;

      case GamepadInputType.button:
        _onButtonInputEvent(event);
        break;
    }
  }

  /// Called when an axis input is received from a gamepad.
  void _onAxisInputEvent(GamepadInputEvent event) {
    final int? unrealIndex = _unrealIndexByGamepadId[event.gamepadId];
    if (unrealIndex == null) {
      return;
    }

    _dirtyAxisInputs[unrealIndex]?.add(event.input);
  }

  /// Called when a button input is received from a gamepad.
  void _onButtonInputEvent(GamepadInputEvent event) {
    final int? unrealIndex = _unrealIndexByGamepadId[event.gamepadId];
    if (unrealIndex == null) {
      return;
    }

    final int? inputId = _inputIds[event.input];
    if (inputId == null) {
      return;
    }

    if (event.value > 0) {
      client.sendOnDataChannel(
        PixelStreamingToStreamerMessageKind.gamepadButtonPressed,
        StreamerGamepadButtonPressedMessage(
          controllerIndex: unrealIndex,
          buttonIndex: inputId,
          bIsRepeat: false, // Currently unused
        ),
      );
    } else {
      client.sendOnDataChannel(
        PixelStreamingToStreamerMessageKind.gamepadButtonReleased,
        StreamerGamepadButtonReleasedMessage(
          controllerIndex: unrealIndex,
          buttonIndex: inputId,
        ),
      );
    }
  }

  /// Send the current value of each reported axis on the gamepad with the given [gamepadId] via WebRTC data channel.
  void _sendGamepadAxes(int gamepadId) {
    final int? unrealIndex = _unrealIndexByGamepadId[gamepadId];

    if (unrealIndex == null) {
      return;
    }

    final Set<GamepadInput>? dirtyAxes = _dirtyAxisInputs[unrealIndex];
    if (dirtyAxes == null || dirtyAxes.isEmpty) {
      return;
    }

    final gamepadApi = GamepadApi.instance;

    for (final axis in dirtyAxes) {
      final int? inputId = _inputIds[axis];

      if (inputId == null) {
        continue;
      }

      final double value = gamepadApi.getAxisValue(gamepadId, axis);

      client.sendOnDataChannel(
        PixelStreamingToStreamerMessageKind.gamepadAnalog,
        StreamerGamepadAnalogMessage(
          controllerIndex: unrealIndex,
          analogIndex: inputId,
          value: value,
        ),
      );
    }

    dirtyAxes.clear();
  }

  /// Send a connection message for all existing controllers.
  void _sendAllControllers() {
    final gamepadApi = GamepadApi.instance;

    for (final controller in gamepadApi.connectedIds) {
      _onGamepadConnected(controller);
    }
  }
}
