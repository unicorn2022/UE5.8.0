// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.api

import RtcIceCandidate
import RtcPeerConnectionFlutterApi
import RtcPeerConnectionHostApi
import RtcSessionDescription
import RtcStatsReport
import android.content.Context
import android.media.AudioAttributes
import android.util.Log
import com.epicgames.live_link_vcam.MainActivity
import com.epicgames.live_link_vcam.util.FlutterPluginApi
import com.epicgames.live_link_vcam.util.IFlutterPluginApi
import com.epicgames.live_link_vcam.util.IdObjectManager
import com.epicgames.live_link_vcam.webrtc.flutter.FlutterRtcMediaStreamTrack
import com.epicgames.live_link_vcam.webrtc.flutter.FlutterRtcPeerConnection
import com.epicgames.live_link_vcam.webrtc.util.WebRtcUtils
import io.flutter.plugin.common.BinaryMessenger
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.Logging
import org.webrtc.PeerConnection
import org.webrtc.PeerConnection.RTCConfiguration
import org.webrtc.PeerConnectionFactory
import org.webrtc.audio.JavaAudioDeviceModule

/**
 * Handles messages about [FlutterRtcPeerConnection]s to and from the Flutter API.
 */
class FlutterRtcPeerConnectionApi(
    /** Messenger used to communicate with Flutter. */
    binaryMessenger: BinaryMessenger,

    /** Context in which this was created. */
    context: Context
) : RtcPeerConnectionHostApi,
    IFlutterPluginApi<RtcPeerConnectionFlutterApi> by FlutterPluginApi(
        RtcPeerConnectionFlutterApi(
            binaryMessenger
        )
    ) {

    /**
     * The application context in which this was created.
     * Note that this is guaranteed to survive the entire application's existence (as opposed to the passed-in context,
     * which may only be tied to e.g. an activity), so this avoids a memory leak.
     */
    private val applicationContext = context.applicationContext

    /** Manager for indexed peer connections shared with Flutter. */
    private val peerConnectionManager by lazy {
        object : IdObjectManager<FlutterRtcPeerConnection>("PeerConnection") {}
    }

    /** Manager for indexed media stream tracks shared with Flutter. */
    val mediaStreamTrackManager by lazy {
        object : IdObjectManager<FlutterRtcMediaStreamTrack>("MediaStreamTrack") {}
    }

    /** Video decoder used for incoming video streams. */
    private val videoDecoderFactory by lazy {
        DefaultVideoDecoderFactory(MainActivity.eglBaseContext)
    }

    /** Field trials passed from Flutter to be used when creating connections. */
    private var fieldTrialsString: String? = null

    /** Factory used to create PeerConnections as requested by the client. */
    private var _peerConnectionFactory: PeerConnectionFactory? = null
    private var peerConnectionFactory: PeerConnectionFactory
        get() {
            if (_peerConnectionFactory == null) {
                peerConnectionFactory = makePeerConnectionFactory()
            }

            return _peerConnectionFactory!!
        }
        set(value) {
            _peerConnectionFactory?.dispose()
            _peerConnectionFactory = value
        }

    /** Set up messaging with Flutter. */
    init {
        RtcPeerConnectionHostApi.setUp(binaryMessenger, this)
    }

    /** Clean up any allocated resources. */
    fun disposeAll() {
        peerConnectionManager.disposeAll()
        mediaStreamTrackManager.disposeAll()
        cancelCoroutines()

        _peerConnectionFactory?.dispose()
    }

    //region WebRtcHostApi
    override fun setFieldTrials(fieldTrials: Map<String, String>?) {
        fieldTrialsString = if (fieldTrials == null) {
            null
        } else {
            WebRtcUtils.formatFieldTrials(fieldTrials)
        }

        // Dispose of existing factory since the settings were changed.
        // This will be lazily recreated.
        _peerConnectionFactory?.dispose()
        _peerConnectionFactory = null
    }

    override fun create(): Long {
        val config = RTCConfiguration(arrayListOf())
        config.apply {
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
            continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_CONTINUALLY
        }

        return peerConnectionManager.register(
            FlutterRtcPeerConnection(
                api = this,
                peerConnectionFactory = peerConnectionFactory,
                config = config,
            )
        )
    }

    override fun dispose(connectionId: Long) {
        peerConnectionManager.getChecked(connectionId).onUnregistered()
        peerConnectionManager.unregister(connectionId)
    }

    override fun setRemoteDescription(
        connectionId: Long,
        description: RtcSessionDescription,
        callback: (Result<Unit>) -> Unit
    ) {
        doWorkAndRespondOnUiThread(callback) {
            peerConnectionManager
                .getChecked(connectionId)
                .setRemoteDescription(description)
        }
    }

    override fun setLocalDescription(
        connectionId: Long,
        description: RtcSessionDescription,
        callback: (Result<Unit>) -> Unit
    ) {
        doWorkAndRespondOnUiThread(callback) {
            peerConnectionManager
                .getChecked(connectionId)
                .setLocalDescription(description)
        }
    }

    override fun addRemoteCandidate(connectionId: Long, candidate: RtcIceCandidate, callback: (Result<Unit>) -> Unit) {
        doWorkAndRespondOnUiThread(callback) {
            peerConnectionManager
                .getChecked(connectionId)
                .addRemoteCandidate(candidate)
        }
    }

    override fun createAnswer(connectionId: Long, callback: (Result<RtcSessionDescription>) -> Unit) {
        doWorkAndRespondOnUiThread(callback) {
            peerConnectionManager
                .getChecked(connectionId)
                .createAnswer()
        }
    }

    override fun getStats(connectionId: Long, typeFilter: List<String>?, callback: (Result<RtcStatsReport>) -> Unit) {
        doWorkAndRespondOnUiThread(callback) {
            peerConnectionManager
                .getChecked(connectionId)
                .getStats(typeFilter)
        }
    }
    //endregion

    /** Make a new peer connection factory with the currently stored settings. */
    private fun makePeerConnectionFactory(): PeerConnectionFactory {
        val optionsBuilder: PeerConnectionFactory.InitializationOptions.Builder =
            PeerConnectionFactory.InitializationOptions.builder(applicationContext)
                .setInjectableLogger({ message, severity, label ->
                    when (severity) {
                        Logging.Severity.LS_VERBOSE -> {
                            Log.v("PeerConnectionFactory", "label: $label, message: $message")
                        }

                        Logging.Severity.LS_INFO -> {
                            Log.i("PeerConnectionFactory", "label: $label, message: $message")
                        }

                        Logging.Severity.LS_WARNING -> {
                            Log.w("PeerConnectionFactory", "label: $label, message: $message")
                        }

                        Logging.Severity.LS_ERROR -> {
                            Log.e("PeerConnectionFactory", "label: $label, message: $message")
                        }

                        Logging.Severity.LS_NONE -> {
                            Log.d("PeerConnectionFactory", "label: $label, message: $message")
                        }

                        else -> {}
                    }
                }, Logging.Severity.LS_WARNING)

        if (fieldTrialsString != null) {
            optionsBuilder.setFieldTrials(fieldTrialsString)
        }

        PeerConnectionFactory.initialize(
            optionsBuilder.createInitializationOptions()
        )

        return PeerConnectionFactory.builder()
            .setVideoDecoderFactory(videoDecoderFactory)
            .setAudioDeviceModule(
                JavaAudioDeviceModule
                    .builder(applicationContext)
                    .setUseLowLatency(true)
                    .setAudioAttributes(
                        AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .build()
                    )
                    .createAudioDeviceModule().also { audioDeviceModule ->
                        audioDeviceModule.setSpeakerMute(false)
                    }
            )
            .createPeerConnectionFactory()
    }
}
