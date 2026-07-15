// Copyright Epic Games, Inc. All Rights Reserved.

import '../../models/native/native_object_manager.dart';
import '../native/peer_connection.dart';
import 'webrtc_api.g.dart';

/// Singleton accessor class that handles communication about [RtcPeerConnection]s with the host WebRTC API.
class WebRtcPeerConnectionApi extends RtcPeerConnectionFlutterApi {
  /// Private constructor.
  WebRtcPeerConnectionApi._() : host = RtcPeerConnectionHostApi() {
    RtcPeerConnectionFlutterApi.setup(this);
  }

  /// Instance of the singleton API.
  static late final WebRtcPeerConnectionApi _instance = WebRtcPeerConnectionApi._();

  /// Instance of the singleton API.
  static WebRtcPeerConnectionApi get instance => _instance;

  /// The WebRtc API that communicates with the host's native plugin.
  final RtcPeerConnectionHostApi host;

  /// The manager that tracks native instances of [RtcPeerConnection]s.
  final manager = NativeObjectManager<RtcPeerConnection>();

  @override
  void onIceCandidate(int connectionId, RtcIceCandidate? candidate) {
    if (candidate != null) {
      manager.get(connectionId)?.onLocalIceCandidate(candidate);
    }
  }

  @override
  void onStateChanged(int connectionId, RtcPeerConnectionState state) {
    manager.get(connectionId)?.onConnectionStateChanged(state);
  }

  @override
  void onTrack(int connectionId, RtcTrackEvent event) {
    manager.get(connectionId)?.onTrack(event);
  }

  @override
  void onDataChannel(int connectionId, int dataChannelId) {
    manager.get(connectionId)?.onDataChannel(dataChannelId);
  }
}
