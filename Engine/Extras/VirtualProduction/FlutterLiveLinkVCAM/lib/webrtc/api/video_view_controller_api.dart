// Copyright Epic Games, Inc. All Rights Reserved.

import '../../models/native/native_object_manager.dart';
import '../native/video_view_controller.dart';
import 'webrtc_api.g.dart';

/// Singleton accessor class that handles communication about [RtcVideoViewController]s with the host WebRTC API.
class RtcVideoViewControllerApi extends RtcVideoViewControllerFlutterApi {
  /// Private constructor.
  RtcVideoViewControllerApi._() : host = RtcVideoViewControllerHostApi() {
    RtcVideoViewControllerFlutterApi.setup(this);
  }

  /// Instance of the singleton API.
  static late final RtcVideoViewControllerApi _instance = RtcVideoViewControllerApi._();

  /// Instance of the singleton API.
  static RtcVideoViewControllerApi get instance => _instance;

  /// The WebRtc API that communicates with the host's native plugin.
  final RtcVideoViewControllerHostApi host;

  /// The manager that tracks native instances of [RtcVideoViewController]s.
  final manager = NativeObjectManager<RtcVideoViewController>();

  @override
  void onFrameSizeChanged(int controllerId, int width, int height) {
    manager.get(controllerId)?.onFrameSizeChanged(width, height);
  }

  @override
  void onFirstFrameRendered(int controllerId) {
    manager.get(controllerId)?.onFirstFrameRendered();
  }
}
