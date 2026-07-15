// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:pigeon/pigeon.dart';

@ConfigurePigeon(PigeonOptions(
  copyrightHeader: 'pigeons/copyright.txt',
  dartOut: 'lib/gamepad/api/gamepad_api.g.dart',
  kotlinOut: 'android/app/src/main/kotlin/com/epicgames/live_link_vcam/gamepad/api/GamepadApi.g.kt',
  kotlinOptions: KotlinOptions(errorClassName: 'FlutterGamepadError'),
  swiftOut: 'IOS/Runner/Gamepad/API/GamepadApi.g.swift',
))

/// Inputs that can be reported in a [GamepadInputEvent].
enum GamepadInput {
  dpadUp,
  dpadDown,
  dpadLeft,
  dpadRight,
  faceButtonBottom,
  faceButtonRight,
  faceButtonLeft,
  faceButtonTop,
  shoulderButtonLeft,
  shoulderButtonRight,
  triggerButtonLeft,
  triggerButtonRight,
  thumbstickLeftButton,
  thumbstickRightButton,
  specialButtonLeft,
  specialButtonRight,
  triggerAxisLeft,
  triggerAxisRight,
  thumbstickLeftX,
  thumbstickLeftY,
  thumbstickRightX,
  thumbstickRightY,
}

/// Types of input that can be reported in a [GamepadInputEvent].
enum GamepadInputType {
  button,
  axis,
}

/// An event sent when a gamepad receives input.
class GamepadInputEvent {
  const GamepadInputEvent({
    required this.gamepadId,
    required this.input,
    required this.type,
    required this.value,
  });

  /// The identifier of the gamepad that received the input.
  final int gamepadId;

  /// The input received.
  final GamepadInput input;

  /// The type of input received.
  final GamepadInputType type;

  /// The value of the input.
  /// For buttons, this will be either 0 (unpressed) or 1 (pressed).
  /// For axes, this ranges from 0 to 1.
  final double value;
}

/// API that receives messages about gamepad input in the host language.
@HostApi()
abstract class GamepadHostApi {
  /// Get the current list of connected gamepad IDs.
  List<int> getActiveGamepadIds();
}

/// API that receives messages about gamepad input in Flutter.
@FlutterApi()
abstract class GamepadFlutterApi {
  /// Called when a gamepad connects to the app.
  void onGamepadConnected(int gamepadId);

  /// Called when a gamepad disconnects from the app.
  void onGamepadDisconnected(int gamepadId);

  /// Called when a gamepad receives input.
  void onGamepadInputEvent(GamepadInputEvent event);
}
