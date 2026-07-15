// Copyright Epic Games, Inc. All Rights Reserved.

import '../../models/native/native_object_manager.dart';
import '../native/data_channel.dart';
import 'webrtc_api.g.dart';

/// Singleton accessor class that handles communication about [RtcDataChannel]s with the host WebRTC API.
class RtcDataChannelApi extends RtcDataChannelFlutterApi {
  /// Private constructor.
  RtcDataChannelApi._() : host = RtcDataChannelHostApi() {
    RtcDataChannelFlutterApi.setup(this);
  }

  /// Instance of the singleton API.
  static late final RtcDataChannelApi _instance = RtcDataChannelApi._();

  /// Instance of the singleton API.
  static RtcDataChannelApi get instance => _instance;

  /// The WebRtc API that communicates with the host's native plugin.
  final RtcDataChannelHostApi host;

  /// The manager that tracks native instances of [RtcDataChannel]s.
  final manager = NativeObjectManager<RtcDataChannel>();

  @override
  void onMessage(int dataChannelId, RtcDataBuffer buffer) {
    manager.get(dataChannelId)?.onMessage(buffer);
  }

  @override
  void onStateChanged(int dataChannelId, RtcDataChannelState state) {
    manager.get(dataChannelId)?.onStateChanged(state);
  }
}
