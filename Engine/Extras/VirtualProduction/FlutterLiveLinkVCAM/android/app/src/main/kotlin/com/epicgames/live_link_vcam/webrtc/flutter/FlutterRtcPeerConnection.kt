// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.flutter

import RtcIceCandidate
import RtcSessionDescription
import RtcStats
import RtcStatsReport
import RtcTrackEvent
import com.epicgames.live_link_vcam.MainActivity
import com.epicgames.live_link_vcam.util.IdObject
import com.epicgames.live_link_vcam.webrtc.api.FlutterRtcPeerConnectionApi
import com.epicgames.live_link_vcam.webrtc.util.WebRtcUtils
import com.epicgames.live_link_vcam.webrtc.util.toFlutter
import com.epicgames.live_link_vcam.webrtc.util.toNative
import org.webrtc.DataChannel
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.MediaStream
import org.webrtc.MediaStreamTrack
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.RTCStatsReport
import org.webrtc.RtpTransceiver
import org.webrtc.SessionDescription
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

/** A wrapped PeerConnection that communicates events to the Flutter WebRTC API. */
class FlutterRtcPeerConnection(
    /** The API used to send messages to Flutter. */
    private val api: FlutterRtcPeerConnectionApi,

    /** The factory used to create a new peer connection. */
    private val peerConnectionFactory: PeerConnectionFactory,

    /** The RTC configuration to use for this connection. */
    private val config: PeerConnection.RTCConfiguration,
) : IdObject(), PeerConnection.Observer {
    /** The underlying WebRTC peer connection. */
    private lateinit var connection: PeerConnection

    /** IDs of stream tracks belonging to this connection. */
    private val trackIds: MutableList<Long> = mutableListOf()

    /**
     * Set the description of the remote peer.
     * @param description The session description.
     */
    suspend fun setRemoteDescription(description: RtcSessionDescription): Result<Unit> =
        WebRtcUtils.setSdp { sdpObserver ->
            connection.setRemoteDescription(
                sdpObserver,
                SessionDescription(
                    description.type.toNative(),
                    description.sdp
                )
            )
        }

    /**
     * Set the description of the local peer.
     * @param description The session description.
     */
    suspend fun setLocalDescription(description: RtcSessionDescription): Result<Unit> =
        WebRtcUtils.setSdp { sdpObserver ->
            connection.setLocalDescription(
                sdpObserver,
                SessionDescription(
                    description.type.toNative(),
                    description.sdp
                )
            )
        }

    /**
     * Add an ICE candidate for connecting to the remote peer.
     * @param candidate The candidate to add.
     */
    fun addRemoteCandidate(candidate: RtcIceCandidate): Result<Unit> {
        val success: Boolean = connection.addIceCandidate(
            IceCandidate(
                candidate.sdpMid,
                candidate.sdpMLineIndex.toInt(),
                candidate.candidate
            )
        )

        return if (success) {
            Result.success(Unit)
        } else {
            Result.failure(Error("Failed to add remote candidate"))
        }
    }

    /**
     * Create an answer to an offered WebRTC session.
     */
    suspend fun createAnswer(): Result<RtcSessionDescription> {
        val result: Result<SessionDescription> = WebRtcUtils.createSdp { sdpObserver ->
            connection.createAnswer(sdpObserver, MediaConstraints())
        }

        if (result.isFailure) {
            return Result.failure(result.exceptionOrNull()!!)
        }

        val description: SessionDescription = result.getOrNull()!!
        return Result.success(
            RtcSessionDescription(
                type = description.type.toFlutter(),
                sdp = description.description
            )
        )
    }

    /**
     * Gather a WebRTC stats report for this connection.
     */
    suspend fun getStats(typeFilter: List<String>?): Result<RtcStatsReport> {
        val nativeReport = suspendCoroutine<RTCStatsReport> { continuation ->
            connection.getStats { report ->
                continuation.resume(report)
            }
        }

        val statsMap: MutableMap<String?, RtcStats?> = mutableMapOf()

        for (statsEntry in nativeReport.statsMap.entries) {
            val stats = statsEntry.value

            // Skip types that don't pass the filter
            if (typeFilter != null && !typeFilter.contains(stats.type)) {
                continue
            }

            val members: MutableMap<String?, Any?> = stats.members.toMutableMap()

            // Workaround: message codec can't handle Java arrays of strings, so convert them to Kotlin-native lists
            members.replaceAll { _: String?, value: Any? ->
                if (value?.javaClass?.isArray == true && value.javaClass.componentType == String::class.java) {
                    return@replaceAll (value as Array<*>).toList()
                }

                return@replaceAll value
            }

            statsMap[statsEntry.key] = RtcStats(
                timestampUs = stats.timestampUs,
                type = stats.type,
                id = stats.id,
                values = members,
            )
        }

        return Result.success(RtcStatsReport(nativeReport.timestampUs, statsMap))
    }

    //region IndexedObject
    override fun onIdReady(): Boolean {
        connection = peerConnectionFactory.createPeerConnection(config, this)
            ?: return false

        return true
    }

    override fun dispose() {
        for (trackId in trackIds) {
            api.mediaStreamTrackManager.unregister(trackId)
        }

        connection.dispose()
    }
    //endregion

    //region PeerConnection.Observer
    override fun onIceCandidate(candidate: IceCandidate?) {
        var data: RtcIceCandidate? = null

        if (candidate != null) {
            data = RtcIceCandidate(
                candidate = candidate.sdp,
                sdpMid = candidate.sdpMid,
                sdpMLineIndex = candidate.sdpMLineIndex.toLong()
            )
        }

        api.callFlutter { flutter ->
            flutter.onIceCandidate(id, data) {}
        }
    }

    override fun onConnectionChange(newState: PeerConnection.PeerConnectionState?) {
        assert(newState != null)

        api.callFlutter { flutter ->
            flutter.onStateChanged(id, newState!!.toFlutter()) {}
        }
    }

    override fun onTrack(transceiver: RtpTransceiver?) {
        val track: MediaStreamTrack = transceiver?.receiver?.track() ?: return
        val trackId: Long = api.mediaStreamTrackManager.register(FlutterRtcMediaStreamTrack(track))
        trackIds.add(trackId)

        api.callFlutter { flutter ->
            flutter.onTrack(
                id, RtcTrackEvent(
                    trackId = trackId,
                    kind = WebRtcUtils.parseTrackKind(track.kind()),
                )
            ) {}
        }
    }

    override fun onDataChannel(channel: DataChannel?) {
        if (channel == null) {
            return
        }

        val dataChannel: FlutterRtcDataChannel = MainActivity.dataChannelApi.register(channel)

        api.callFlutter { flutter ->
            flutter.onDataChannel(id, dataChannel.id) {}
        }
    }

    override fun onSignalingChange(newState: PeerConnection.SignalingState?) {}
    override fun onIceConnectionChange(newState: PeerConnection.IceConnectionState?) {}
    override fun onIceConnectionReceivingChange(receiving: Boolean) {}
    override fun onIceGatheringChange(newState: PeerConnection.IceGatheringState?) {}
    override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}
    override fun onAddStream(stream: MediaStream?) {}
    override fun onRemoveStream(stream: MediaStream?) {}
    override fun onRenegotiationNeeded() {}
    //endregion
}
