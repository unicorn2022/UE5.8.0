// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:logging/logging.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:web_socket_channel/io.dart';

import '../../webrtc/data/pixel_streaming_to_client_message.dart';
import '../../webrtc/data/pixel_streaming_to_streamer_message.dart';
import '../../webrtc/native/data_channel.dart';
import '../../webrtc/util/webrtc_extension.dart';
import '../../webrtc/webrtc.dart';
import 'connection_enum.dart';
import 'webrtc_stat_history.dart';

final _log = Logger('UnrealWebRtcClient');

/// How long to wait between WebRTC stat report requests.
const _statReportInterval = Duration(seconds: 1);

/// Client for communication with Unreal Editor's WebRtc pixel streaming protocol.
/// Handles initial negotiation/signaling, then provides video and data streams.
class UnrealWebRtcClient {
  UnrealWebRtcClient() {
    _webSocketMessageHandlers['config'] = _onReceiveConfig;
    _webSocketMessageHandlers['offer'] = _onReceiveOffer;
    _webSocketMessageHandlers['iceCandidate'] = _onReceiveRemoteIceCandidate;

    listenToDataChannel(PixelStreamingToClientMessageKind.qualityControlOwnership, _onQualityControlOwnership);
  }

  /// Statistics for the incoming video stream.
  final videoStreamStats = WebRtcStatHistory();

  /// Stream controller for [connectionState].
  final _connectionState = ValueNotifier<EngineConnectionState>(EngineConnectionState.disconnected);

  /// Stream controller for [videoTrack].
  final _videoTrackController = StreamController<RtcMediaStreamTrack?>.broadcast();

  /// Value notifier for [resolution].
  final _resolution = ValueNotifier<Size?>(null);

  /// List of streamers available on the current connection.
  final List<String> _streamers = [];

  /// State of the client's WebRTC connection. Use [_state] to set this variable so events trigger.
  EngineConnectionState _internalState = EngineConnectionState.disconnected;

  /// HTTP client of the current WebSocket connection or connection attempt.
  HttpClient? _webSocketHttpClient;

  /// The websocket channel we currently have open to the editor.
  IOWebSocketChannel? _webSocketChannel;

  /// The current subscription to stream events from the WebSocket connection.
  StreamSubscription? _webSocketSubscription;

  /// The current WebRtc peer connection.
  RtcPeerConnection? _peerConnection;

  /// Whether the peer connection is ready to receive remote candidates.
  bool _bIsReadyForCandidates = false;

  /// Whether the pending connection has been cancelled.
  bool _bIsPendingConnectionCancelled = false;

  /// Map from WebSocket message name to function that will handle the message when received.
  Map<String, Function(Map<String, dynamic>)> _webSocketMessageHandlers = {};

  /// Map from WebRTC data channel message ID to the stream controller that broadcasts when the message is received.
  Map<PixelStreamingToClientMessageKind, StreamController<dynamic>> _dataChannelStreams = {};

  /// Remote candidates received before the peer connection was ready.
  List<RtcIceCandidate> _earlyCandidates = [];

  /// The active data channel.
  RtcDataChannel? _dataChannel;

  /// Subscriptions to streams that will be cancelled when the data channel changes.
  List<StreamSubscription> _dataChannelSubscriptions = [];

  /// Subscriptions to streams that will be cancelled when the peer connection is disposed.
  List<StreamSubscription> _peerConnectionSubscriptions = [];

  /// Timer used to gather stats reports from WebRTC.
  Timer? _statReportTimer;

  /// Completes when the client has a data channel.
  var _hasDataChannel = Completer();

  /// State of the client's connection, which encompasses both the WebSocket signaling and WebRTC peer connections.
  EngineConnectionState get state => _internalState;

  /// Completes when the client has a data channel.
  Future get hasDataChannel => _hasDataChannel.future;

  /// The last requested stream resolution in the current connection session.
  ValueListenable<Size?> get resolution => _resolution;

  /// List of streamers available on the current connection.
  UnmodifiableListView<String> get streamers => UnmodifiableListView(_streamers);

  void set _state(EngineConnectionState value) {
    if (value == state) {
      return;
    }
    _internalState = value;
    _connectionState.value = _internalState;
  }

  /// The state of the WebRTC connection.
  ValueListenable<EngineConnectionState> get connectionState => _connectionState;

  /// The active video stream track.
  Stream<RtcMediaStreamTrack?> get videoTrack => _videoTrackController.stream;

  /// Connect to the WebRTC signaling server with the given [host] address and [port].
  /// Returns a future which completes with true when initial handshake is complete and the streamer list has been
  /// received.
  /// If the future completes with [EngineConnectionResult.needsStreamer], the signaling server reported multiple
  /// streamers. In that case, you must call [subscribeToStreamer], passing in one of the streamer IDs in [streamers] to
  /// complete the connection.
  Future<EngineConnectionResult> connect(InternetAddress host, int port) async {
    if (state != EngineConnectionState.disconnected) {
      return EngineConnectionResult.alreadyOpen;
    }

    _bIsPendingConnectionCancelled = false;
    _state = EngineConnectionState.connecting;

    _disposePeerConnection();
    _earlyCandidates.clear();

    final bool bHasWebSocket = await _connectWebSocket(host, port);

    if (_bIsPendingConnectionCancelled) {
      disconnect();
      return EngineConnectionResult.cancelled;
    }

    if (!bHasWebSocket) {
      _log.severe('Failed to open WebSocket connection to ${host.address}:$port');
      disconnect();
      return EngineConnectionResult.webSocketFailure;
    }

    final streamerList = await _getStreamerList();
    _streamers.clear();
    _streamers.addAll(streamerList.map((streamer) => streamer.toString()));

    if (_bIsPendingConnectionCancelled) {
      disconnect();
      return EngineConnectionResult.cancelled;
    }

    if (streamerList.isEmpty) {
      _log.severe('No streamers available; aborting connection');
      disconnect();
      return EngineConnectionResult.noStreamers;
    }

    if (streamerList.length == 1) {
      subscribeToStreamer(streamerList.first);
    } else {
      _log.info('Waiting for user to select a streamer');
      _state = EngineConnectionState.waitingForStreamer;
      return EngineConnectionResult.needsStreamer;
    }

    return EngineConnectionResult.success;
  }

  /// Subscribe to one of the available streamers with the given [streamerId].
  void subscribeToStreamer(String streamerId) {
    _log.info('Subscribing to $streamerId');
    _sendWebSocketMessage('subscribe', {'streamerId': streamerId});
  }

  /// Indicate that we should cancel the pending connection attempt, if any.
  void cancelPendingConnection() {
    if (state == EngineConnectionState.connecting) {
      _bIsPendingConnectionCancelled = true;

      // This will force the WebSocket channel to close immediately rather than waiting to time out
      _webSocketHttpClient?.close(force: true);
    }
  }

  /// Disconnect from the editor.
  void disconnect() {
    _hasDataChannel = Completer();

    _webSocketChannel?.sink.close();
    _webSocketChannel = null;

    _webSocketSubscription?.cancel();
    _webSocketSubscription = null;

    _webSocketHttpClient?.close(force: true);
    _webSocketHttpClient = null;

    _disposePeerConnection();
    _bIsReadyForCandidates = false;

    _statReportTimer?.cancel();

    _videoTrackController.add(null);

    _streamers.clear();
    _earlyCandidates.clear();
    _resolution.value = null;

    _unsubscribeFromDataChannel();

    _state = EngineConnectionState.disconnected;
  }

  /// Shut down the client and release all resources.
  void dispose() {
    disconnect();
    _videoTrackController.close();
    _connectionState.dispose();
  }

  /// Add a message [handler] for the given [messageKind] on WebRTC data channels.
  /// These handlers will persist even if a new data channel is created, so they can be added before the channel exists.
  StreamSubscription listenToDataChannel(PixelStreamingToClientMessageKind messageKind, Function(dynamic) handler) {
    final stream = _dataChannelStreams.putIfAbsent(messageKind, () => StreamController<dynamic>.broadcast()).stream;
    return stream.listen(handler);
  }

  /// Send a [message] of the given [messageKind] on the active data channel.
  /// If no [message] is provided, the message ID will be sent with no additional data.
  /// Returns false if no data channel was available.
  bool sendOnDataChannel(
    PixelStreamingToStreamerMessageKind messageKind, [
    PixelStreamingToStreamerMessage? message,
  ]) {
    if (_dataChannel == null) {
      return false;
    }

    if (message != null) {
      _dataChannel?.sendData(message.encode(messageKind.id));
    } else {
      _dataChannel?.sendData(Uint8List.fromList([messageKind.id]));
    }

    return true;
  }

  /// Change the resolution of the WebRTC streamer's video stream.
  void setResolution(Size newResolution) {
    if (newResolution == resolution.value) {
      return;
    }

    _resolution.value = newResolution;

    final message = StreamerCommandMessage(command: {
      'resolution': {
        'width': newResolution.width,
        'height': newResolution.height,
      }
    });

    _log.info('Sending remote resolution (${newResolution.width.round()}x${newResolution.height.round()})');
    sendOnDataChannel(PixelStreamingToStreamerMessageKind.command, message);
  }

  /// Called when a WebSocket message is received.
  void _onWebSocketMessage(dynamic message) {
    final String stringData = String.fromCharCodes(message);

    final dynamic jsonMessage;
    try {
      jsonMessage = jsonDecode(stringData);
    } catch (error) {
      _log.warning('Failed to parse WebSocket message:\n$error\nMessage text:$stringData');
      return;
    }

    final String type = jsonMessage['type'];
    _webSocketMessageHandlers[type]?.call(jsonMessage);
  }

  /// Called when a WebSocket error occurs.
  void _onWebSocketError(dynamic error) {
    _log.severe('WebSocket error', error);
  }

  /// Called when the WebSocket connection is closed.
  void _onWebSocketStreamClosed() {
    // Force the HTTP client itself shut in case we were in the middle of connecting and want to immediately stop.
    // The WebSocket itself isn't returned until the connection completes, so we have to kill the connection from here.
    _webSocketHttpClient?.close(force: true);
    _webSocketHttpClient = null;

    _webSocketChannel = null;
    _webSocketSubscription = null;
  }

  /// Called when we receive a peer connection configuration. Receives JSON [data] from the message.
  void _onReceiveConfig(Map<String, dynamic> data) async {
    final receivedIceServers = data['iceServers'];
    _log.info('Received peer connection configuration. ICE servers:\n$receivedIceServers');

    // TODO: Handle ICE servers. Engine doesn't currently send these.
  }

  /// Called when we receive a WebRtc signaling offer. Receives JSON [data] from the message.
  void _onReceiveOffer(Map<String, dynamic> data) async {
    _log.info('Received remote SDP:\n$data');

    final String sdpType = data['type'];
    if (data['type'] != 'offer') {
      _log.info('We only support replying to offer, but we got $sdpType');
      return;
    }

    RtcSessionDescription remoteDescription;
    try {
      remoteDescription = RtcSessionDescription(type: RtcSessionDescriptionType.offer, sdp: data['sdp']);
    } catch (e) {
      _log.severe('Invalid session description: $e');
      disconnect();
      return;
    }

    _bIsReadyForCandidates = false;

    await _setUpPeerConnection();
    await _peerConnection!.setRemoteDescription(remoteDescription);

    // Generate local description
    final RtcSessionDescription localDescription = await _peerConnection!.createAnswer();
    await _peerConnection!.setLocalDescription(localDescription);

    _bIsReadyForCandidates = true;

    // Add any remote candidates that were received before this was ready
    for (final RtcIceCandidate candidate in _earlyCandidates) {
      _peerConnection!.addRemoteIceCandidate(candidate);
    }
    _earlyCandidates.clear();

    final String mungedSdp = await _addSessionIdToSdp(localDescription.sdp);

    _log.info('Sending answer SDP:\n$mungedSdp');

    _sendWebSocketMessage('answer', {
      'type': 'answer',
      'sdp': mungedSdp,
    });
  }

  /// Called when we the peer connection finds a local WebRtc ICE candidate.
  void _onFoundLocalIceCandidate(RtcIceCandidate candidate) async {
    final candidateData = {
      'candidate': candidate.candidate,
      'sdpMid': candidate.sdpMid,
      'sdpMlineIndex': candidate.sdpMLineIndex,
    };

    _log.info('Found local ICE candidate:\n$candidateData');

    _sendWebSocketMessage('iceCandidate', {
      'candidate': candidateData,
    });
  }

  /// Called when we receive a remote WebRtc ICE candidate. Receives JSON [data] from the message.
  void _onReceiveRemoteIceCandidate(Map<String, dynamic> data) {
    final candidateData = data['candidate'];

    _log.info('Received remote ICE candidate:\n$candidateData');

    late final candidate;
    try {
      candidate = RtcIceCandidate(
        candidate: candidateData['candidate'],
        sdpMid: candidateData['sdpMid'],
        sdpMLineIndex: candidateData['sdpMLineIndex'],
      );
    } catch (e) {
      _log.severe('Invalid candidate: $e');
      disconnect();
      return;
    }

    if (_bIsReadyForCandidates) {
      _peerConnection!.addRemoteIceCandidate(candidate);
    } else {
      _earlyCandidates.add(candidate);
    }
  }

  /// Called when a new remote streaming track is added.
  void _onRemoteTrack(RtcTrackEvent event) {
    switch (event.kind) {
      case RtcMediaStreamTrackKind.video:
        _videoTrackController.add(
          RtcMediaStreamTrack(id: event.trackId, kind: event.kind),
        );
        break;

      default:
        break;
    }
  }

  /// Called when a new data channel is added.
  void _onDataChannel(RtcDataChannel newDataChannel) {
    _dataChannel = newDataChannel;

    if (!_hasDataChannel.isCompleted) {
      _hasDataChannel.complete();
    }

    _dataChannelSubscriptions.addAll([
      _dataChannel!.stateChangedEvent.listen(_onDataChannelStateChanged),
      _dataChannel!.messageEvent.listen(_onDataChannelMessage),
    ]);
  }

  /// Called when the state of the current data channel changes.
  void _onDataChannelStateChanged(RtcDataChannelState state) {
    if (state != RtcDataChannelState.closed) {
      return;
    }

    disconnect();
  }

  /// Called when a message is received on the current data channel. The message data is contained in the [buffer].
  void _onDataChannelMessage(RtcDataBuffer buffer) {
    try {
      final int id = buffer.byteData.getUint8(0);

      final messageKind = PixelStreamingToClientMessageKind.withId(id);
      final message = messageKind?.parse?.call(buffer);

      if (message != null) {
        _dataChannelStreams[messageKind]?.add(message);
      }
    } catch (e) {
      _log.severe('Failed to parse an incoming DataChannel message: ');
    }
  }

  /// Called when the peer connection [state] changes.
  void _onPeerConnectionStateChanged(RtcPeerConnectionState state) {
    _log.info('Peer connection state changed: $state');

    // Note that we ignore the "disconnected" state, which may just be temporary connection loss. If we remain
    // disconnected for too long, we'll instead move to "failed" or "closed," at which point we actually need to restart
    // the WebRTC connection.
    // This also means we may enter the "connected" state multiple times in a single session if we become temporarily
    // disconnected.

    switch (state) {
      case RtcPeerConnectionState.connected:
        if (_internalState == EngineConnectionState.connecting) {
          // Finished connecting, so move to connected state and restart stats
          _state = EngineConnectionState.connected;

          videoStreamStats.reset();
          if (_statReportTimer?.isActive != true) {
            _statReportTimer = Timer.periodic(_statReportInterval, (_) => _gatherWebRtcStats());
          }
        }
        break;

      case RtcPeerConnectionState.closed:
      case RtcPeerConnectionState.failed:
        disconnect();

      default:
        break;
    }
  }

  /// Send a WebSocket message with a given [type].
  /// If [data] is provided, the [type] will be add to it as a field; otherwise, a message will be created containing
  /// only the [type].
  void _sendWebSocketMessage(String type, [Map<String, dynamic>? data]) {
    final Map<String, dynamic> message = {
      'type': type,
    };

    if (data != null) {
      message.addAll(data);
    }

    _webSocketChannel?.sink.add(jsonEncode(message));
  }

  /// Create a WebSocket connection.
  Future<bool> _connectWebSocket(InternetAddress host, int port) async {
    final String address = 'ws://${host.address}:${port}';
    WebSocket? webSocket;

    _log.info('Connecting to WebSocket at $address');

    // Try to connect
    String lastError = '';

    try {
      webSocket = await WebSocket.connect(
        address,
        customClient: _makeHttpClient(),
      );
    } catch (error) {
      _webSocketHttpClient?.close(force: true);
      _webSocketHttpClient = null;

      lastError = error.toString();
      _log.info('Connection failed: $lastError');
      return false;
    }

    _log.info('Connected successfully to $address');

    _webSocketChannel = IOWebSocketChannel(webSocket);
    _webSocketSubscription = _webSocketChannel!.stream.listen(
      _onWebSocketMessage,
      onError: _onWebSocketError,
      onDone: _onWebSocketStreamClosed,
    );

    return true;
  }

  /// Make and remember a new HTTP client to use for WebSocket connections.
  HttpClient _makeHttpClient() {
    assert(_webSocketHttpClient == null);

    _webSocketHttpClient = HttpClient();
    _webSocketHttpClient!.connectionTimeout = Duration(seconds: 3);

    return _webSocketHttpClient!;
  }

  /// Get a list of streamers from the signaling server.
  Future<List> _getStreamerList() async {
    // Set up handling for streamer list before we send the message
    final streamerListCompleter = Completer<Map<String, dynamic>>();
    _webSocketMessageHandlers['streamerList'] = (data) {
      streamerListCompleter.complete(data);
    };

    // Request a list of streamers
    _log.info('Requesting streamer list...');
    _sendWebSocketMessage('listStreamers');

    final data = await streamerListCompleter.future.timeout(
      const Duration(seconds: 3),
      onTimeout: () => {},
    );
    final streamerList = data['ids'];

    _log.info('Got streamer list: $streamerList');

    if (!(streamerList is List)) {
      _log.severe('Invalid streamer list');
      disconnect();
      return [];
    }

    _webSocketMessageHandlers.remove('streamerList');

    return streamerList;
  }

  /// Set up the peer connection.
  Future _setUpPeerConnection() async {
    _disposePeerConnection();

    final newPeerConnection = RtcPeerConnection();
    await newPeerConnection.initialize();

    _peerConnection = newPeerConnection;
    _peerConnectionSubscriptions = [
      _peerConnection!.iceCandidateEvent.listen(_onFoundLocalIceCandidate),
      _peerConnection!.trackEvent.listen(_onRemoteTrack),
      _peerConnection!.dataChannelEvent.listen(_onDataChannel),
      _peerConnection!.connectionStateChangeEvent.listen(_onPeerConnectionStateChanged),
    ];
  }

  /// Dispose of the current peer connection.
  void _disposePeerConnection() {
    if (_peerConnection == null) {
      return;
    }

    _peerConnectionSubscriptions.forEach((subscription) => subscription.cancel());
    _peerConnection!.dispose();
    _peerConnection = null;
  }

  /// Add a unique identifier for the LiveLink app to the given [sdp] string.
  Future<String> _addSessionIdToSdp(String sdp) async {
    final packageInfo = await PackageInfo.fromPlatform();

    // Make a string that is like: s=LiveLink-iOS/1.3.2(130)
    final identifier = 's=LiveLink/${Platform.operatingSystem}/${packageInfo.version}(${packageInfo.buildNumber})';

    // Replace "s=-" which is the session id line in the SDP
    return sdp.replaceAll('s=-', identifier);
  }

  /// Forget the current data channel and stop listening to it (if it exists).
  void _unsubscribeFromDataChannel() {
    _dataChannelSubscriptions.forEach((subscription) => subscription.cancel());
    _dataChannel = null;
  }

  /// Called when we receive a QualityControlOwnership message via WebRTC data channel.
  void _onQualityControlOwnership(dynamic message) {
    if (message is! ClientQualityControlOwnershipMessage) {
      return;
    }

    _log.info('VCAM is quality controller: ${message.bControlsQuality}');

    // Immediately request quality control if we don't hold it
    if (!message.bControlsQuality) {
      _log.info('Requesting quality control');
      sendOnDataChannel(PixelStreamingToStreamerMessageKind.requestQualityControl);
    }
  }

  /// Gather WebRTC stats and add them to the stat history.
  void _gatherWebRtcStats() async {
    if (_peerConnection == null) {
      return;
    }

    final RtcStatsReport report = await _peerConnection!.getStats(typeFilter: ['inbound-rtp']);
    final RtcStats? videoStats = report.stats.values.firstWhere(
      (stats) => stats?.values['kind'] == 'video',
      orElse: () => null,
    );

    if (videoStats != null) {
      videoStreamStats.processStats(videoStats);
    }
  }
}
