// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.util

import RtcMediaStreamTrackKind
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

object WebRtcUtils {
    /**
     * Convenience function that creates an SdpObserver and passes it through to the provided call in a suspended
     * coroutine, resuming when the SDP is either set or fails to set.
     *
     * @param call The function into which to pass the SdpObserver.
     */
    suspend inline fun setSdp(
        crossinline call: (SdpObserver) -> Unit
    ): Result<Unit> = suspendCoroutine {
        call(object : SdpObserver {
            // Handle set events and pass through to the coroutine
            override fun onSetSuccess() =
                it.resume(Result.success(Unit))

            override fun onSetFailure(error: String?) =
                it.resume(Result.failure(RuntimeException(error)))

            // Ignore create events for a set operation
            override fun onCreateSuccess(sdp: SessionDescription?) {}
            override fun onCreateFailure(error: String?) {}
        })
    }

    /**
     * Convenience function that creates an SdpObserver and passes it through to the provided call in a suspended
     * coroutine, resuming with either a successfully created session description or a failure.
     *
     * @param call The function into which to pass the SdpObserver.
     */
    suspend inline fun createSdp(
        crossinline call: (SdpObserver) -> Unit
    ): Result<SessionDescription> = suspendCoroutine {
        call(object : SdpObserver {
            // Handle create events and pass through to the coroutine
            override fun onCreateSuccess(sdp: SessionDescription?) {
                if (sdp == null) {
                    it.resume(Result.failure(RuntimeException("Got null SessionDescription")))
                } else {
                    it.resume(Result.success(sdp))
                }
            }

            override fun onCreateFailure(error: String?) =
                it.resume(Result.failure(RuntimeException(error)))

            // Ignore set events for a create operation
            override fun onSetSuccess() {}
            override fun onSetFailure(error: String?) {}
        })
    }

    /**
     * Given the string describing a WebRTC media track kind, return the corresponding enum value.
     */
    fun parseTrackKind(string: String): RtcMediaStreamTrackKind = when (string) {
        "audio" -> RtcMediaStreamTrackKind.AUDIO
        "video" -> RtcMediaStreamTrackKind.VIDEO
        else -> throw Exception("Invalid media track kind '$string'")
    }

    /**
     * Convert a key/value map of field trials to the string format expected by WebRTC.
     */
    fun formatFieldTrials(fieldTrials: Map<String, String>): String =
        fieldTrials.entries.joinToString(separator = "/", postfix = "/") { pair ->
            "${pair.key}/${pair.value}"
        }
}
