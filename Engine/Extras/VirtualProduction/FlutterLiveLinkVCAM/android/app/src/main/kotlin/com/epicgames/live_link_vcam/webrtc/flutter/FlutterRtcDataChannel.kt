// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.flutter

import RtcDataBuffer
import com.epicgames.live_link_vcam.util.IdObject
import com.epicgames.live_link_vcam.webrtc.api.FlutterRtcDataChannelApi
import com.epicgames.live_link_vcam.webrtc.util.toFlutter
import org.webrtc.DataChannel
import java.nio.ByteBuffer

/** A wrapped WebRTC data channel which communicates with Flutter. */
class FlutterRtcDataChannel(
    /** The DataChannel this wraps. */
    private val channel: DataChannel,

    /** The API used to send messages to Flutter. */
    private val api: FlutterRtcDataChannelApi,
) : IdObject(), DataChannel.Observer {
    init {
        channel.registerObserver(this)
    }

    override fun dispose() {}

    /** Send a message on the data channel. */
    fun sendMessage(buffer: RtcDataBuffer) {
        val outBuffer = ByteBuffer.wrap(buffer.data)

        channel.send(DataChannel.Buffer(outBuffer, buffer.bIsBinary))
    }

    //region DataChannel.Observer
    override fun onBufferedAmountChange(previousAmount: Long) {}

    override fun onStateChange() {
        val newState = channel.state()

        api.callFlutter { flutter ->
            flutter.onStateChanged(id, newState.toFlutter()) {}
        }

        if (channel.state() == DataChannel.State.CLOSED) {
            api.dataChannelManager.unregister(id)
        }
    }

    override fun onMessage(buffer: DataChannel.Buffer?) {
        if (buffer == null) {
            return
        }

        val byteArray = ByteArray(buffer.data.capacity())
        buffer.data.get(byteArray)

        api.callFlutter { flutter ->
            flutter.onMessage(
                id, RtcDataBuffer(
                    byteArray,
                    buffer.binary,
                )
            ) {}
        }
    }
    //endregion
}
