// Copyright Epic Games, Inc. All Rights Reserved.

import '../../models/native/native_object_manager.dart';
import '../native/session.dart';
import 'ar_api.g.dart';

/// Singleton accessor class that handles communication about [ArSession]s with the host AR API.
class ArSessionApi extends ArSessionFlutterApi {
  /// Private constructor.
  ArSessionApi._() : host = ArSessionHostApi() {
    ArSessionFlutterApi.setup(this);
  }

  /// Instance of the singleton API.
  static late final ArSessionApi _instance = ArSessionApi._();

  /// Instance of the singleton API.
  static ArSessionApi get instance => _instance;

  /// The API that communicates with the host's native plugin.
  final ArSessionHostApi host;

  /// The manager that tracks native instances of [RtcPeerConnection]s.
  final manager = NativeObjectManager<ArSession>();

  @override
  void onFrame(int sessionId, ArFrame frame) {
    manager.get(sessionId)?.onFrame(frame);
  }

  @override
  void onTrackingStateChanged(int sessionId, ArTrackingState state) {
    manager.get(sessionId)?.onTrackingStateChanged(state);
  }
}
