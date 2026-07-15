// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:pigeon/pigeon.dart';

@ConfigurePigeon(PigeonOptions(
  copyrightHeader: 'pigeons/copyright.txt',
  dartOut: 'lib/webrtc/api/webrtc_api.g.dart',
  kotlinOut: 'android/app/src/main/kotlin/com/epicgames/live_link_vcam/webrtc/api/WebRtcApi.g.kt',
  swiftOut: 'IOS/Runner/WebRTC/API/WebRtcApi.g.swift',
))

/// Supported types for [RtcSessionDescription].
enum RtcSessionDescriptionType {
  answer,
  offer,
  pranswer,
  rollback,
}

/// State of an [RtcPeerConnection]'s connection to the corresponding peer.
enum RtcPeerConnectionState {
  newConnection,
  connecting,
  connected,
  disconnected,
  failed,
  closed,
}

/// Kinds of MediaStreamTrack that can be created.
enum RtcMediaStreamTrackKind {
  audio,
  video,
}

/// State of an [RtcDataChannel].
enum RtcDataChannelState {
  connecting,
  open,
  closing,
  closed,
}

/// A buffer passed from/to WebRTC, which may contain either binary or text data.
class RtcDataBuffer {
  const RtcDataBuffer({
    required this.data,
    required this.bIsBinary,
  });

  /// The binary data contained in the buffer.
  final Uint8List data;

  /// True if the buffer contains binary data; otherwise, it contains UTF-8 text.
  final bool bIsBinary;
}

/// Describes the configuration of one end of a peer-to-peer WebRtc connection.
class RtcSessionDescription {
  RtcSessionDescription(this.type, this.sdp);

  /// The type of description.
  final RtcSessionDescriptionType type;

  /// The SDP information describing the session.
  final String sdp;
}

/// A candidate for interactive connectivity establishment (ICE) to a peer.
class RtcIceCandidate {
  const RtcIceCandidate({
    required this.candidate,
    required this.sdpMid,
    required this.sdpMLineIndex,
  });

  /// A string describing the candidate (taken from the SDP "candidate" field).
  final String candidate;

  /// A string specifying the candidate's unique media stream ID tag.
  final String sdpMid;

  /// The index number of the media description within the SDP.
  final int sdpMLineIndex;
}

/// Event data for when a track is added to a stream in a [RtcPeerConnection].
class RtcTrackEvent {
  const RtcTrackEvent({
    required this.trackId,
    required this.kind,
  });

  /// The unique ID of the media track used to refer to it when communicating with the native plugin.
  final int trackId;

  /// The kind of media in this track.
  final RtcMediaStreamTrackKind kind;
}

/// Reported statistics about a single object inspected as part of a WebRTC report.
class RtcStats {
  const RtcStats({
    required this.timestampUs,
    required this.type,
    required this.id,
    required this.values,
  });

  /// Timestamp at which the stats were gathered (in microseconds).
  final double timestampUs;

  /// The type of object for which these stats were collected.
  final String type;

  /// The unique ID of the stats object.
  final String id;

  /// Map from stat name to its value.
  final Map<String?, Object?> values;
}

/// A collection of reported statistics gathered about a WebRTC peer connection.
class RtcStatsReport {
  const RtcStatsReport({
    required this.timestampUs,
    required this.stats,
  });

  /// Timestamp at which the stats were gathered (in microseconds).
  final double timestampUs;

  /// Map from object name to the stats associated with it.
  final Map<String?, RtcStats?> stats;
}

/// API that receives messages about RtcPeerConnection instances in the host language.
@HostApi()
abstract class RtcPeerConnectionHostApi {
  /// Set the field trial keys and values enabled for future PeerConnections.
  void setFieldTrials(Map<String, String>? fieldTrials);

  /// Create a PeerConnection.
  /// Returns an ID which can be used to refer to the native object in future calls.
  int create();

  /// Dispose of the PeerConnection with the given [connectionId].
  void dispose(int connectionId);

  /// Set the [description] of the remote peer for the PeerConnection with the given [connectionId].
  @async
  void setRemoteDescription(int connectionId, RtcSessionDescription description);

  /// Set the [description] of the local peer for the PeerConnection with the given [connectionId].
  @async
  void setLocalDescription(int connectionId, RtcSessionDescription description);

  /// Add an ICE [candidate] for connecting to the remote peer.
  @async
  void addRemoteCandidate(int connectionId, RtcIceCandidate candidate);

  /// Create an answer to a WebRTC offer for the PeerConnection with the given [connectionId].
  @async
  RtcSessionDescription createAnswer(int connectionId);

  /// Generate a stats report about the PeerConnection with the given [connectionId].
  /// If [typeFilter] is provided, the report will only contain stats matching types in that list, reducing the codec
  /// overhead from the native platform.
  @async
  RtcStatsReport getStats(int connectionId, List<String>? typeFilter);
}

/// API that receives messages about RtcPeerConnection instances in Flutter.
@FlutterApi()
abstract class RtcPeerConnectionFlutterApi {
  /// Called when a new local ICE [candidate] is found for the PeerConnection with the given [connectionId].
  void onIceCandidate(int connectionId, RtcIceCandidate? candidate);

  /// Called when the [state] of the PeerConnection with the given [connectionId] changes.
  void onStateChanged(int connectionId, RtcPeerConnectionState state);

  /// Called when a MediaStreamTrack (described in [event]) is added to the PeerConnection with the given
  /// [connectionId].
  void onTrack(int connectionId, RtcTrackEvent event);

  /// Called when a DataChannel with the given [dataChannelId] is added to the PeerConnection with the given
  /// [connectionId].
  void onDataChannel(int connectionId, int dataChannelId);
}

/// API that receives messages about RtcDataChannel instances in the host language.
@HostApi()
abstract class RtcDataChannelHostApi {
  /// Send a message contained in [buffer] on the data channel with the given [dataChannelId].
  void sendMessage(int dataChannelId, RtcDataBuffer buffer);
}

/// API that receives messages about RtcDataChannel instances in Flutter.
@FlutterApi()
abstract class RtcDataChannelFlutterApi {
  /// Called when the [state] of the DataChannel with the given [dataChannelId] changes.
  void onStateChanged(int dataChannelId, RtcDataChannelState state);

  /// Called when a message contained in [buffer] is received on the data channel with the given [dataChannelId].
  void onMessage(int dataChannelId, RtcDataBuffer buffer);
}

/// API that receives messages about VideoViewController instances in the host language.
@HostApi()
abstract class RtcVideoViewControllerHostApi {
  /// Create a VideoViewController.
  /// Returns an ID which can be used to refer to the native object in future calls.
  int create();

  /// Set the video track of the VideoViewController with the given [controllerId] to the track with the given
  /// [trackId].
  void setTrack(int controllerId, int trackId);

  /// Get the texture ID associated with the VideoViewController with the given [controllerId].
  int getTextureId(int controllerId);

  /// Clear the image of the VideoViewController with the given [controllerId].
  void clear(int controllerId);

  /// Dispose of the VideoViewController with the given [controllerId].
  void dispose(int controllerId);
}

/// API that receives messages about VideoViewController instances in Flutter.
@FlutterApi()
abstract class RtcVideoViewControllerFlutterApi {
  /// Called when the VideoViewController with the given [controllerId] has rendered its first frame since it was
  /// created or cleared (whichever is latest).
  void onFirstFrameRendered(int controllerId);

  /// Called when the size of a VideoViewController's frame size changes.
  void onFrameSizeChanged(int controllerId, int width, int height);
}
