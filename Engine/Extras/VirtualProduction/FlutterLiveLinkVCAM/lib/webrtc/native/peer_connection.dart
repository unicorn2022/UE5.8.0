// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import '../../models/native/native_object.dart';
import '../../models/native/native_object_manager.dart';
import '../api/peer_connection_api.dart';
import '../api/webrtc_api.g.dart';
import 'data_channel.dart';

/// A WebRtc connection between this computer and a remote peer.
class RtcPeerConnection extends NativeObject {
  RtcPeerConnection({WebRtcPeerConnectionApi? api}) : this.api = api ?? WebRtcPeerConnectionApi.instance;

  /// The API used to communicate with the host language.
  final WebRtcPeerConnectionApi api;

  /// Stream controller for [iceCandidateEvent].
  final StreamController<RtcIceCandidate> _iceCandidateEvent = StreamController.broadcast();

  /// Stream controller for [trackEvent].
  final StreamController<RtcTrackEvent> _trackEvent = StreamController.broadcast();

  /// Stream controller for [connectionStateChangeEvent].
  final StreamController<RtcPeerConnectionState> _connectionStateChangeEvent = StreamController.broadcast();

  /// Stream controller for [dataChannelEvent].
  final StreamController<RtcDataChannel> _dataChannelEvent = StreamController.broadcast();

  /// The current state of the connection.
  RtcPeerConnectionState _state = RtcPeerConnectionState.newConnection;

  @override
  NativeObjectManager<NativeObject> get manager => api.manager;

  /// The current state of the connection.
  RtcPeerConnectionState get state => _state;

  /// A stream that broadcasts whenever a local ICE candidate is found.
  Stream<RtcIceCandidate> get iceCandidateEvent => _iceCandidateEvent.stream;

  /// A stream that broadcasts whenever a new media track becomes available.
  Stream<RtcTrackEvent> get trackEvent => _trackEvent.stream;

  /// A stream that broadcasts whenever the peer connection state changes.
  Stream<RtcPeerConnectionState> get connectionStateChangeEvent => _connectionStateChangeEvent.stream;

  /// A stream that broadcasts whenever a data channel is added.
  Stream<RtcDataChannel> get dataChannelEvent => _dataChannelEvent.stream;

  /// Set the session description for the remote peer.
  Future<void> setRemoteDescription(RtcSessionDescription description) async =>
      api.host.setRemoteDescription(id, description);

  /// Set the session description for the local computer.
  Future<void> setLocalDescription(RtcSessionDescription description) async =>
      api.host.setLocalDescription(id, description);

  /// Add an ICE candidate which may be used to connect to the remote peer.
  Future<void> addRemoteIceCandidate(RtcIceCandidate candidate) async => api.host.addRemoteCandidate(id, candidate);

  /// Create an answer to a connection offer.
  /// Returns a description of this computer's end of the connection.
  Future<RtcSessionDescription> createAnswer() async => api.host.createAnswer(id);

  /// Generate a stats report for this connection's streams.
  /// If [typeFilter] is provided, the report will only contain stats matching types in that list, reducing the codec
  /// overhead from the native platform.
  Future<RtcStatsReport> getStats({List<String>? typeFilter}) async => api.host.getStats(id, typeFilter);

  /// Called when a local ICE [candidate] is found for this connection.
  void onLocalIceCandidate(RtcIceCandidate candidate) => _iceCandidateEvent.add(candidate);

  /// Called when the [state] of the connection changes.
  void onConnectionStateChanged(RtcPeerConnectionState state) {
    _state = state;
    _connectionStateChangeEvent.add(state);
  }

  /// Called when a track is added to the connection and generates [event] data.
  void onTrack(RtcTrackEvent event) => _trackEvent.add(event);

  /// Called when a data channel is added to the connection with the given [dataChannelId].
  void onDataChannel(int dataChannelId) async {
    final dataChannel = RtcDataChannel(nativeId: dataChannelId);
    await dataChannel.initialize();
    _dataChannelEvent.add(dataChannel);
  }

  @override
  Future<int> internalInit() => api.host.create();

  @override
  Future<void> internalDispose() async {
    await api.host.dispose(id);

    // Wait for the connection to close so we can send its final event
    if (_state != RtcPeerConnectionState.closed) {
      final closedCompleter = Completer();
      connectionStateChangeEvent.listen((_) => closedCompleter.complete());

      await closedCompleter.future;
    }

    _iceCandidateEvent.close();
    _trackEvent.close();
    _connectionStateChangeEvent.close();
  }
}
