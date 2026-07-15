// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.webrtc.flutter

import com.epicgames.live_link_vcam.MainActivity
import com.epicgames.live_link_vcam.util.IdObject
import com.epicgames.live_link_vcam.webrtc.api.FlutterRtcVideoViewControllerApi
import io.flutter.view.TextureRegistry.SurfaceTextureEntry
import org.webrtc.EglBase
import org.webrtc.EglRenderer
import org.webrtc.GlRectDrawer
import org.webrtc.VideoFrame
import org.webrtc.VideoSink
import org.webrtc.VideoTrack

/**
 * Controller for a WebRTC video view which renders a video stream to a shared texture shared with Flutter and
 * communicates related messages.
 */
class FlutterRtcVideoViewController(
    /** The API used to send messages to Flutter. */
    private val api: FlutterRtcVideoViewControllerApi,
) : IdObject(), VideoSink {
    /** Cached name used for logging. */
    private val loggingName by lazy { "FlutterRtcVideoViewController #$id" }

    /** The current video track being rendered. */
    private var track: VideoTrack? = null

    /** The width of the last rendered frame. */
    private var lastWidth: Int = 0

    /** The height of the last rendered frame. */
    private var lastHeight: Int = 0

    /** Whether this has reported its first frame since being created/cleared. */
    private var bHasReportedFirstFrame: Boolean = false

    /** Renderer used to render the video. */
    private val eglRenderer: EglRenderer = EglRenderer(loggingName)

    /** The texture to share with Flutter. */
    private val textureEntry: SurfaceTextureEntry =
        MainActivity.textureRegistry.createSurfaceTexture()

    /** The ID of the output texture bound to the Flutter engine. */
    val textureId: Long get() = textureEntry.id()

    init {
        eglRenderer.init(MainActivity.eglBaseContext, EglBase.CONFIG_PLAIN, GlRectDrawer())
        eglRenderer.createEglSurface(textureEntry.surfaceTexture())
    }

    /**
     * Set the Flutter track to be displayed.
     * @param newTrack The new track to display.
     */
    fun setTrack(newTrack: FlutterRtcMediaStreamTrack?) {
        lastWidth = 0
        lastHeight = 0
        track?.removeSink(this)

        val newVideoTrack: VideoTrack? = if (newTrack != null) {
            newTrack.inner as? VideoTrack ?: throw Exception("Track '${newTrack.inner.id()}' is not a video track")
        } else {
            null
        }

        newVideoTrack?.addSink(this)
        track = newVideoTrack
    }

    /** Clear the rendered video stream texture. */
    fun clear() {
        bHasReportedFirstFrame = false
        eglRenderer.clearImage()
    }

    override fun dispose() {
        track?.removeSink(this)
        eglRenderer.release()
        textureEntry.release()
    }

    //region VideoSink
    override fun onFrame(frame: VideoFrame?) {
        if (frame == null) {
            return
        }

        // Update Flutter and renderer with new size
        if (frame.rotatedWidth != lastWidth || frame.rotatedHeight != lastHeight) {
            lastWidth = frame.rotatedWidth
            lastHeight = frame.rotatedHeight

            api.callFlutter { flutter ->
                flutter.onFrameSizeChanged(id, lastWidth.toLong(), lastHeight.toLong()) {}
            }

            textureEntry.surfaceTexture().setDefaultBufferSize(lastWidth, lastHeight)
        }

        // Render the latest frame
        eglRenderer.onFrame(frame)

        // Report the first frame if we haven't already
        if (!bHasReportedFirstFrame) {
            api.callFlutter { flutter ->
                flutter.onFirstFrameRendered(id) {}
            }

            bHasReportedFirstFrame = true
        }
    }
    //endregion
}
