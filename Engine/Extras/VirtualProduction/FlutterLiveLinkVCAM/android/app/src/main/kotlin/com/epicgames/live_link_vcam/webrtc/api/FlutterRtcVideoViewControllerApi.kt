// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.api

import RtcVideoViewControllerFlutterApi
import RtcVideoViewControllerHostApi
import com.epicgames.live_link_vcam.MainActivity
import com.epicgames.live_link_vcam.util.FlutterPluginApi
import com.epicgames.live_link_vcam.util.IFlutterPluginApi
import com.epicgames.live_link_vcam.util.IdObject
import com.epicgames.live_link_vcam.util.IdObjectManager
import com.epicgames.live_link_vcam.webrtc.flutter.FlutterRtcMediaStreamTrack
import com.epicgames.live_link_vcam.webrtc.flutter.FlutterRtcVideoViewController
import io.flutter.plugin.common.BinaryMessenger
import kotlinx.coroutines.launch

/**
 * Handles messages about [FlutterRtcVideoViewController]s to and from the Flutter API.
 */
class FlutterRtcVideoViewControllerApi(
    /** Messenger used to communicate with Flutter. */
    binaryMessenger: BinaryMessenger,
) : RtcVideoViewControllerHostApi,
    IFlutterPluginApi<RtcVideoViewControllerFlutterApi> by FlutterPluginApi(
        RtcVideoViewControllerFlutterApi(
            binaryMessenger
        )
    ) {

    /** Manager for indexed video view controllers shared with Flutter. */
    private val videoViewControllerManager by lazy {
        object : IdObjectManager<FlutterRtcVideoViewController>("VideoViewController") {}
    }

    /** Set up messaging with Flutter. */
    init {
        RtcVideoViewControllerHostApi.setUp(binaryMessenger, this)
    }

    /** Clean up any allocated resources. */
    fun dispose() {
        videoViewControllerManager.disposeAll()
        cancelCoroutines()
    }

    //region WebRtcHostApi
    override fun create(): Long {
        return videoViewControllerManager.register(FlutterRtcVideoViewController(this))
    }

    override fun setTrack(controllerId: Long, trackId: Long) {
        val track: FlutterRtcMediaStreamTrack?

        if (trackId == IdObject.invalidId) {
            track = null
        } else {
            track = MainActivity.peerConnectionApi.mediaStreamTrackManager.get(trackId)
            assert(track != null)
        }

        workCoroutineScope.launch {
            videoViewControllerManager.getChecked(controllerId).setTrack(track)
        }
    }

    override fun getTextureId(controllerId: Long): Long {
        return videoViewControllerManager.getChecked(controllerId).textureId
    }

    override fun clear(controllerId: Long) {
        videoViewControllerManager.getChecked(controllerId).clear()
    }

    override fun dispose(controllerId: Long) {
        videoViewControllerManager.unregister(controllerId)
    }
    //endregion
}
