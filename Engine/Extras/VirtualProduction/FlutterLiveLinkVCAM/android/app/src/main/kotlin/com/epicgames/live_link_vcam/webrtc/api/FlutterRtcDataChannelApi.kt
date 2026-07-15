// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.api

import RtcDataBuffer
import RtcDataChannelFlutterApi
import RtcDataChannelHostApi
import com.epicgames.live_link_vcam.util.FlutterPluginApi
import com.epicgames.live_link_vcam.util.IFlutterPluginApi
import com.epicgames.live_link_vcam.util.IdObjectManager
import com.epicgames.live_link_vcam.webrtc.flutter.FlutterRtcDataChannel
import io.flutter.plugin.common.BinaryMessenger
import org.webrtc.DataChannel

/**
 * Handles messages about [FlutterRtcDataChannel]s to and from the Flutter API.
 */
class FlutterRtcDataChannelApi(
    /** Messenger used to communicate with Flutter. */
    binaryMessenger: BinaryMessenger,
) : RtcDataChannelHostApi,
    IFlutterPluginApi<RtcDataChannelFlutterApi> by FlutterPluginApi(
        RtcDataChannelFlutterApi(
            binaryMessenger
        )
    ) {

    /** Manager for indexed video view controllers shared with Flutter. */
    val dataChannelManager by lazy {
        object : IdObjectManager<FlutterRtcDataChannel>("DataChannel") {}
    }

    /** Set up messaging with Flutter. */
    init {
        RtcDataChannelHostApi.setUp(binaryMessenger, this)
    }

    /** Clean up any allocated resources. */
    fun dispose() {
        dataChannelManager.disposeAll()
        cancelCoroutines()
    }

    /** Convenience function to register a new WebRTC-provided data channel. */
    fun register(dataChannel: DataChannel): FlutterRtcDataChannel {
        val flutterDataChannel = FlutterRtcDataChannel(
            api = this,
            channel = dataChannel,
        )

        dataChannelManager.register(flutterDataChannel)

        return flutterDataChannel
    }

    //region RtcDataChannelHostApi
    override fun sendMessage(dataChannelId: Long, buffer: RtcDataBuffer) {
        dataChannelManager.getChecked(dataChannelId).sendMessage(buffer)
    }
    //endregion
}
