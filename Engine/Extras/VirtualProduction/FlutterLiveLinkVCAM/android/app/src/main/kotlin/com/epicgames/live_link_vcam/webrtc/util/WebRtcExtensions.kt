// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.util

import RtcDataChannelState
import RtcPeerConnectionState
import RtcSessionDescriptionType
import org.webrtc.DataChannel
import org.webrtc.PeerConnection.PeerConnectionState
import org.webrtc.SessionDescription

/** Convert from a Flutter session description type to the native WebRTC equivalent. */
fun RtcSessionDescriptionType.toNative(): SessionDescription.Type = when (this) {
    RtcSessionDescriptionType.ANSWER -> SessionDescription.Type.ANSWER
    RtcSessionDescriptionType.OFFER -> SessionDescription.Type.OFFER
    RtcSessionDescriptionType.PRANSWER -> SessionDescription.Type.PRANSWER
    RtcSessionDescriptionType.ROLLBACK -> SessionDescription.Type.ROLLBACK
}

/** Convert from the native WebRTC session description type to the Flutter equivalent. */
fun SessionDescription.Type.toFlutter(): RtcSessionDescriptionType = when (this) {
    SessionDescription.Type.ANSWER -> RtcSessionDescriptionType.ANSWER
    SessionDescription.Type.OFFER -> RtcSessionDescriptionType.OFFER
    SessionDescription.Type.PRANSWER -> RtcSessionDescriptionType.PRANSWER
    SessionDescription.Type.ROLLBACK -> RtcSessionDescriptionType.ROLLBACK
}

/** Convert from a Flutter connection state to the native WebRTC equivalent. */
fun RtcPeerConnectionState.toNative(): PeerConnectionState = when (this) {
    RtcPeerConnectionState.NEWCONNECTION -> PeerConnectionState.NEW
    RtcPeerConnectionState.CONNECTING -> PeerConnectionState.CONNECTING
    RtcPeerConnectionState.CONNECTED -> PeerConnectionState.CONNECTED
    RtcPeerConnectionState.DISCONNECTED -> PeerConnectionState.DISCONNECTED
    RtcPeerConnectionState.FAILED -> PeerConnectionState.FAILED
    RtcPeerConnectionState.CLOSED -> PeerConnectionState.CLOSED
}

/** Convert from the native WebRTC connection state to the Flutter equivalent. */
fun PeerConnectionState.toFlutter(): RtcPeerConnectionState = when (this) {
    PeerConnectionState.NEW -> RtcPeerConnectionState.NEWCONNECTION
    PeerConnectionState.CONNECTING -> RtcPeerConnectionState.CONNECTING
    PeerConnectionState.CONNECTED -> RtcPeerConnectionState.CONNECTED
    PeerConnectionState.DISCONNECTED -> RtcPeerConnectionState.DISCONNECTED
    PeerConnectionState.FAILED -> RtcPeerConnectionState.FAILED
    PeerConnectionState.CLOSED -> RtcPeerConnectionState.CLOSED
}

/** Convert from the native WebRTC data channel state to the Flutter equivalent. */
fun DataChannel.State.toFlutter(): RtcDataChannelState = when (this) {
    DataChannel.State.CONNECTING -> RtcDataChannelState.CONNECTING
    DataChannel.State.OPEN -> RtcDataChannelState.OPEN
    DataChannel.State.CLOSING -> RtcDataChannelState.CLOSING
    DataChannel.State.CLOSED -> RtcDataChannelState.CLOSED
}
