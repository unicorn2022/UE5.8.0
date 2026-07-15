// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:typed_data';

import '../../models/native/native_object.dart';
import '../../models/native/native_object_manager.dart';
import '../../models/native/native_owned_object.dart';
import '../api/data_channel_api.dart';
import '../api/webrtc_api.g.dart';

/// A WebRTC data channel that communicates with the native plugin.
/// These channels are generated and disposed of automatically by the host WebRTC plugin.
class RtcDataChannel extends NativeOwnedObject {
  RtcDataChannel({
    required int nativeId,
    RtcDataChannelApi? api,
  })  : this.api = api ?? RtcDataChannelApi.instance,
        super(nativeId);

  /// The API used to communicate with the host language.
  final RtcDataChannelApi api;

  /// The current state of the channel.
  RtcDataChannelState _state = RtcDataChannelState.connecting;

  /// The current state of the channel.
  RtcDataChannelState get state => _state;

  /// A stream that broadcasts whenever the channel's state changes.
  Stream<RtcDataChannelState> get stateChangedEvent => _stateChangedEvent.stream;

  /// A stream that broadcasts whenever a message is received.
  Stream<RtcDataBuffer> get messageEvent => _messageEvent.stream;

  @override
  NativeObjectManager<NativeObject> get manager => api.manager;

  /// Stream controller for [iceCandidateEvent].
  final StreamController<RtcDataChannelState> _stateChangedEvent = StreamController.broadcast();

  /// Stream controller for [messageEvent].
  final StreamController<RtcDataBuffer> _messageEvent = StreamController.broadcast();

  /// Called when the state changes.
  void onStateChanged(RtcDataChannelState newState) {
    _state = newState;
    _stateChangedEvent.add(newState);

    if (_state == RtcDataChannelState.closed) {
      dispose();
    }
  }

  /// Called when the channel receives a message.
  void onMessage(RtcDataBuffer buffer) {
    _messageEvent.add(buffer);
  }

  /// Send a binary message containing [data] on the data channel.
  void sendData(Uint8List data) {
    api.host.sendMessage(id, RtcDataBuffer(data: data, bIsBinary: true));
  }
}
