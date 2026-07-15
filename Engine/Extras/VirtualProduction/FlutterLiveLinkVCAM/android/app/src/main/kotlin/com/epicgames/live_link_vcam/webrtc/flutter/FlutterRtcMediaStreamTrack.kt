// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.flutter

import com.epicgames.live_link_vcam.util.WrappedIdObject
import org.webrtc.MediaStreamTrack

/**
 * A wrapped WebRTC MediaStreamTrack which can be referred to in Flutter.
 * These tracks are generated and disposed of automatically by WebRTC, so this is just a thin wrapper adding an index.
 */
class FlutterRtcMediaStreamTrack(inner: MediaStreamTrack) : WrappedIdObject<MediaStreamTrack>(inner)
