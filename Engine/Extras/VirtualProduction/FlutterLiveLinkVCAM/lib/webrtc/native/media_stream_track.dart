// Copyright Epic Games, Inc. All Rights Reserved.

import '../api/webrtc_api.g.dart';

/// A single media track within a stream.
/// These tracks are generated and disposed of automatically by the host WebRTC plugin.
class RtcMediaStreamTrack {
  RtcMediaStreamTrack({
    required this.id,
    required this.kind,
  });

  /// The ID used to refer to this track when communicating with the native WebRTC plugin.
  final int id;

  /// The kind of track this represents.
  final RtcMediaStreamTrackKind kind;
}
