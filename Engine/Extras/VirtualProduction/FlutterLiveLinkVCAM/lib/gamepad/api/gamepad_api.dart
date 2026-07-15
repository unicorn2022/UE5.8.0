// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:collection';

import 'gamepad_api.g.dart';

/// Implements the gamepad API interface to provide gamepad event streams.
class GamepadApi extends GamepadFlutterApi {
  /// Private constructor.
  GamepadApi._() : host = GamepadHostApi() {
    GamepadFlutterApi.setup(this);

    // Request list of connected gamepad IDs in case we missed any events
    host.getActiveGamepadIds().then(_onReceiveConnectedIds);
  }

  /// Instance of the singleton API.
  static late final GamepadApi _instance = GamepadApi._();

  /// Instance of the singleton API.
  static GamepadApi get instance => _instance;

  /// The API that communicates with the host's native plugin via a Pigeon platform channel.
  final GamepadHostApi host;

  /// Controller for stream of gamepad input events.
  final _inputEvents = StreamController<GamepadInputEvent>.broadcast();

  /// Controller for stream of newly connected gamepad IDs.
  final _connectedEvents = StreamController<int>.broadcast();

  /// Controller for stream of newly disconnected gamepad IDs.
  final _disconnectedEvents = StreamController<int>.broadcast();

  /// Cached values of gamepad axes for each pair of (gamepadId, input).
  final Map<(int, GamepadInput), double> _axisValues = {};

  /// Cached set of gamepad inputs that have received an axis value from the native platform.
  final Set<GamepadInput> _axes = {};

  /// Native IDs of connected gamepads.
  final Set<int> _activeIds = {};

  /// Stream of gamepad input events.
  Stream<GamepadInputEvent> get inputEvents => _inputEvents.stream;

  /// Stream of newly connected gamepad IDs.
  Stream<int> get connectedEvents => _connectedEvents.stream;

  /// Stream of newly disconnected gamepad IDs.
  Stream<int> get disconnectedEvents => _disconnectedEvents.stream;

  /// Native IDs of connected gamepads.
  UnmodifiableListView<int> get connectedIds => UnmodifiableListView(_activeIds);

  /// Get the value of an [input] axis for the gamepad with the given [gamepadId].
  double getAxisValue(int gamepadId, GamepadInput input) => _axisValues[(gamepadId, input)] ?? 0;

  /// Get the list of gamepad inputs for which this has received an axis value.
  UnmodifiableListView<GamepadInput> get axes => UnmodifiableListView(_axes);

  @override
  void onGamepadConnected(int gamepadId) {
    _connectedEvents.add(gamepadId);
    _activeIds.add(gamepadId);
  }

  @override
  void onGamepadDisconnected(int gamepadId) {
    _disconnectedEvents.add(gamepadId);
    _activeIds.remove(gamepadId);
  }

  @override
  void onGamepadInputEvent(GamepadInputEvent event) {
    if (event.type == GamepadInputType.axis) {
      _axisValues[(event.gamepadId, event.input)] = event.value;
      _axes.add(event.input);
    }

    _inputEvents.add(event);
  }

  /// Called when we first receive the list of connected gamepad IDs.
  void _onReceiveConnectedIds(List<int?> connectedIds) {
    for (final gamepadId in connectedIds) {
      if (gamepadId == null || _activeIds.contains(gamepadId)) {
        continue;
      }

      onGamepadConnected(gamepadId);
    }
  }
}
