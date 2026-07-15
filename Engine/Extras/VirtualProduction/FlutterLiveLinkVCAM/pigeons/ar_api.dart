// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:pigeon/pigeon.dart';

@ConfigurePigeon(PigeonOptions(
  copyrightHeader: 'pigeons/copyright.txt',
  dartOut: 'lib/ar/api/ar_api.g.dart',
  kotlinOut: 'android/app/src/main/kotlin/com/epicgames/live_link_vcam/ar/api/ARApi.g.kt',
  kotlinOptions: KotlinOptions(errorClassName: 'FlutterArError'),
  swiftOut: 'IOS/Runner/AR/API/ARApi.g.swift',
))

/// Data produced during a single frame in an AR session.
class ArFrame {
  const ArFrame({required this.cameraTransformData});

  /// Data for the camera's 4x4 transform matrix.
  final Uint8List cameraTransformData;
}

/// Possible tracking states for an AR session's camera.
enum ArTrackingState {
  /// Tracking data is available.
  normal,

  /// Tracking data may not be accurate.
  limited,

  /// Tracking data is unavailable.
  unavailable,
}

/// Availability of AR features on the device.
enum ArAvailability {
  /// The feature is not available on the user's device.
  notSupported,

  /// The feature may be available on the user's device, but the user declined to install or encountered an error while
  /// doing so.
  notInstalled,

  /// The feature may be available on the user's device, but the user declined camera permission.
  cameraNotPermitted,

  /// The feature is ready to use.
  available,
}

/// API that receives messages about ArSession instances in the host language.
@HostApi()
abstract class ArSessionHostApi {
  /// Request all necessary permissions and/or installation steps for AR functionality.
  /// Must be called before [create].
  /// Completes with the new availability after user agreement completes or fails.
  /// If the platform doesn't require this step, the future completes immediately.
  @async
  ArAvailability initialize();

  /// Create an AR session.
  /// Returns an ID which can be used to refer to the native object in future calls.
  @async
  int create();

  /// Dispose of the AR session with the given [sessionId].
  void dispose(int sessionId);

  /// Run the AR session with the given [sessionId].
  /// It will begin firing delegate events every frame.
  void run(int sessionId);

  /// Pause the AR session with the given [sessionId].
  /// It will stop processing until [run] is called again.
  void pause(int sessionId);
}

/// API that receives messages about ArSession instances in Flutter.
@FlutterApi()
abstract class ArSessionFlutterApi {
  /// Called when the AR session with the given [sessionId] produces data for a new [frame].
  void onFrame(int sessionId, ArFrame frame);

  /// Called when the camera tracking [state] for AR session with the given [sessionId] changes.
  void onTrackingStateChanged(int sessionId, ArTrackingState state);
}
