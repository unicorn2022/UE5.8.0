// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.ar.flutter

import ArFrame
import ArTrackingState
import FlutterArError
import android.content.Context
import android.hardware.display.DisplayManager
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.os.Handler
import android.os.Looper
import android.view.Display
import android.view.OrientationEventListener
import android.view.Surface
import androidx.core.content.ContextCompat.getSystemService
import com.epicgames.live_link_vcam.ar.api.FlutterArSessionApi
import com.epicgames.live_link_vcam.util.IdObject
import com.google.ar.core.Camera
import com.google.ar.core.CameraConfig
import com.google.ar.core.CameraConfigFilter
import com.google.ar.core.Config
import com.google.ar.core.Frame
import com.google.ar.core.Session
import com.google.ar.core.TrackingState
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.EnumSet
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.egl.EGLSurface

/** How long should pass between AR updates (in milliseconds). */
private const val UPDATE_INTERVAL: Long = 16 // 62.5 FPS -- as close as we can get to 60 with integer ms

/** Constant used to identify EGL version. Not provided by the EGL10 API for some reason. */
private const val EGL_VERSION: Int = 0x3098

/** A wrapped AR session that communicates events to the Flutter AR API. */
class FlutterArSession(
    /** The API used to send messages to Flutter. */
    private val api: FlutterArSessionApi,

    /** Context in which this was created. */
    private val context: Context,

    /** Callback function to pass the result of creating this session, passing either the session's ID or an error. */
    private val initCallback: (Result<Long>) -> Unit,
) : IdObject() {
    /** The ARCore session. */
    private var session: Session? = null

    /** The last known tracking state. */
    private var trackingState: ArTrackingState = ArTrackingState.UNAVAILABLE

    /** The display whose orientation this should follow. */
    private val display: Display? =
        getSystemService(context, DisplayManager::class.java)?.getDisplay(Display.DEFAULT_DISPLAY)

    /** Listener for the display's orientation. */
    private val orientationListener: OrientationEventListener

    /** Handler for scheduling AR updates. */
    private val updateHandler: Handler = Handler(Looper.getMainLooper())

    /** The EGL context required for AR updates. */
    private var eglContext: EGLContext = EGL10.EGL_NO_CONTEXT

    /** The EGL display required for AR updates. */
    private var eglDisplay: EGLDisplay = EGL10.EGL_NO_DISPLAY

    /** The EGL surface required for AR updates. */
    private var eglSurface: EGLSurface = EGL10.EGL_NO_SURFACE

    /** The ID of the camera's target texture. */
    private var cameraTextureId: Int? = null

    /** Whether EGL is ready for AR updates to run. */
    private var bIsEglReady: Boolean = false

    /** Whether AR is currently running. */
    private var bIsRunning: Boolean = false

    init {

        initAr()

        // Set up listener for future orientation changes, but leave disabled until AR starts
        orientationListener = object : OrientationEventListener(context) {
            override fun onOrientationChanged(orientation: Int) {
                updateDisplayGeometry()
            }
        }
        orientationListener.disable()
    }

    /** Run the AR session. */
    fun run() {
        if (bIsRunning) {
            return
        }

        orientationListener.enable()
        bIsRunning = true

        session?.resume()
        updateAr()
    }

    /** Pause the AR session. */
    fun pause() {
        if (!bIsRunning) {
            return
        }

        orientationListener.disable()
        bIsRunning = false

        session?.pause()
    }

    //region IndexedObject
    override fun dispose() {
        bIsRunning = false
        orientationListener.disable()

        session?.close()
        disposeEglContext()
    }
    //endregion

    /**
     * Initialize everything required for AR.
     */
    private fun initAr() {
        try {
            initEglContext()
            initArSession()
        } catch (e: Throwable) {
            api.callFlutter {
                initCallback(Result.failure(e))
            }
            return
        }

        api.callFlutter {
            initCallback(Result.success(this.id))
        }
    }

    /**
     * Initialize the EGL context required for AR operations on the current thread.
     * We don't render this to the screen, so we just need the absolute minimum EGL context in order for AR to run.
     */
    private fun initEglContext() {
        val egl = EGLContext.getEGL() as EGL10

        eglDisplay = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY)
        throwEglErrorIfAny()
        assert(eglDisplay != EGL10.EGL_NO_DISPLAY)

        val version = IntArray(2)
        egl.eglInitialize(eglDisplay, version)
        throwEglErrorIfAny()

        // Configure EGL
        // No config specifications -- we don't want any rendering features
        val emptyAttributes = arrayOf(EGL10.EGL_NONE).toIntArray()

        val configs = arrayOf<EGLConfig?>(null)
        val configCount = IntArray(1)

        egl.eglChooseConfig(eglDisplay, emptyAttributes, configs, 1, configCount)
        throwEglErrorIfAny()

        assert(configCount[0] == 1 && configs[0] != null)

        val config: EGLConfig = configs[0]!!

        // Create EGL surface
        eglSurface = egl.eglCreatePbufferSurface(eglDisplay, config, emptyAttributes)
        throwEglErrorIfAny()

        // Create EGL context
        val contextAttributes = arrayOf(EGL_VERSION, 2, EGL10.EGL_NONE).toIntArray()

        eglContext = egl.eglCreateContext(eglDisplay, config, EGL10.EGL_NO_CONTEXT, contextAttributes)
        throwEglErrorIfAny()

        // Make the context current for the thread
        egl.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)
        throwEglErrorIfAny()

        bIsEglReady = true
    }

    /**
     * Initialize the AR session.
     */
    private fun initArSession() {
        session = Session(context)

        val cameraConfigFilter = CameraConfigFilter(session)
        cameraConfigFilter.depthSensorUsage = EnumSet.of(CameraConfig.DepthSensorUsage.DO_NOT_USE)
        cameraConfigFilter.facingDirection = CameraConfig.FacingDirection.BACK
        cameraConfigFilter.stereoCameraUsage = EnumSet.of(CameraConfig.StereoCameraUsage.DO_NOT_USE)

        val cameraConfigs: List<CameraConfig> = session!!.getSupportedCameraConfigs(cameraConfigFilter)
        if (cameraConfigs.isEmpty()) {
            throw FlutterArError("No valid camera configurations found")
        }

        session!!.cameraConfig = cameraConfigs[0]

        val config = Config(session)

        // Disable any extra features we won't use
        config.augmentedFaceMode = Config.AugmentedFaceMode.DISABLED
        config.cloudAnchorMode = Config.CloudAnchorMode.DISABLED
        config.depthMode = Config.DepthMode.DISABLED
        config.focusMode = Config.FocusMode.FIXED
        config.geospatialMode = Config.GeospatialMode.DISABLED
        config.imageStabilizationMode = Config.ImageStabilizationMode.OFF
        config.instantPlacementMode = Config.InstantPlacementMode.DISABLED
        config.lightEstimationMode = Config.LightEstimationMode.DISABLED
        config.planeFindingMode = Config.PlaneFindingMode.DISABLED
        config.semanticMode = Config.SemanticMode.DISABLED
        config.streetscapeGeometryMode = Config.StreetscapeGeometryMode.DISABLED

        // Prevent camera from blocking application updates so it doesn't affect UI/stream frame rate
        config.updateMode = Config.UpdateMode.LATEST_CAMERA_IMAGE

        // Create the camera texture
        val textureIds = IntArray(1)
        GLES20.glGenTextures(1, textureIds, 0)

        cameraTextureId = textureIds[0]
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, cameraTextureId!!)

        session!!.setCameraTextureName(cameraTextureId!!)

        updateDisplayGeometry()
    }

    /**
     * Clean up the AR session's EGL context.
     */
    private fun disposeEglContext() {
        if (eglDisplay != EGL10.EGL_NO_DISPLAY) {
            val egl = EGLContext.getEGL() as EGL10

            egl.eglMakeCurrent(eglDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT)

            if (eglContext != EGL10.EGL_NO_CONTEXT) {
                egl.eglDestroyContext(eglDisplay, eglContext)
                eglContext = EGL10.EGL_NO_CONTEXT
            }

            if (eglSurface != EGL10.EGL_NO_SURFACE) {
                egl.eglDestroySurface(eglDisplay, eglSurface)
                eglSurface = EGL10.EGL_NO_SURFACE
            }

            egl.eglTerminate(eglDisplay)
            eglDisplay = EGL10.EGL_NO_DISPLAY
        }

        if (cameraTextureId != null) {
            GLES20.glDeleteTextures(1, arrayOf(cameraTextureId!!).toIntArray(), 0)
            cameraTextureId = null
        }

        bIsEglReady = false
    }

    /**
     * Update the AR display geometry based on the current device rotation.
     */
    private fun updateDisplayGeometry() {
        // Display must have non-zero geometry, but we never show it to the user, so 1x1 is fine
        session?.setDisplayGeometry(display?.rotation ?: Surface.ROTATION_0, 1, 1)
    }

    /**
     * Update the AR session with new frame data.
     */
    private fun updateAr() {
        if (!bIsRunning) {
            return
        }

        val frame: Frame? = session?.update()
        if (frame != null) {
            onFrame(frame)
        }

        updateHandler.postDelayed({ updateAr() }, UPDATE_INTERVAL)
    }

    /** Called on each frame update from the ARCore session. */
    private fun onFrame(frame: Frame) {
        val camera: Camera = frame.camera

        val newTrackingState: ArTrackingState = when (camera.trackingState) {
            TrackingState.TRACKING -> ArTrackingState.NORMAL
            else -> ArTrackingState.UNAVAILABLE
        }

        // Send new tracking state to Flutter plugin (if it changed)
        if (newTrackingState != trackingState) {
            trackingState = newTrackingState
            api.callFlutter { flutter ->
                flutter.onTrackingStateChanged(this.id, trackingState) {}
            }
        }

        if (trackingState != ArTrackingState.NORMAL) {
            // Tracking data is inaccurate when not in normal mode
            return
        }

        // Send frame data to Flutter plugin
        val viewMatrix = FloatArray(16)
        camera.pose.toMatrix(viewMatrix, 0)

        val viewMatrixBytes = ByteBuffer.allocate(64)
        viewMatrixBytes.order(ByteOrder.nativeOrder())

        for (float: Float in viewMatrix) {
            viewMatrixBytes.putFloat(float)
        }

        api.callFlutter { flutter ->
            flutter.onFrame(this.id, ArFrame(viewMatrixBytes.array())) {}
        }
    }

    /** Throw an exception containing the EGL error message, if any. */
    private fun throwEglErrorIfAny() {
        val egl = EGLContext.getEGL() as EGL10
        val errorCode: Int = egl.eglGetError()

        if (errorCode != EGL10.EGL_SUCCESS) {
            throw FlutterArError(getEglErrorString(errorCode))
        }
    }

    /** Create an error message based on the EGL error code. */
    private fun getEglErrorString(errorCode: Int): String =
        when (errorCode) {
            EGL10.EGL_NOT_INITIALIZED -> "EGL_NOT_INITIALIZED"
            EGL10.EGL_BAD_ACCESS -> "EGL_BAD_ACCESS"
            EGL10.EGL_BAD_ALLOC -> "EGL_BAD_ALLOC"
            EGL10.EGL_BAD_ATTRIBUTE -> "EGL_BAD_ATTRIBUTE"
            EGL10.EGL_BAD_CONTEXT -> "EGL_BAD_CONTEXT"
            EGL10.EGL_BAD_CONFIG -> "EGL_BAD_CONFIG"
            EGL10.EGL_BAD_CURRENT_SURFACE -> "EGL_BAD_CURRENT_SURFACE"
            EGL10.EGL_BAD_DISPLAY -> "EGL_BAD_DISPLAY"
            EGL10.EGL_BAD_SURFACE -> "EGL_BAD_SURFACE"
            EGL10.EGL_BAD_MATCH -> "EGL_BAD_MATCH"
            EGL10.EGL_BAD_PARAMETER -> "EGL_BAD_PARAMETER"
            EGL10.EGL_BAD_NATIVE_PIXMAP -> "EGL_BAD_NATIVE_PIXMAP"
            EGL10.EGL_BAD_NATIVE_WINDOW -> "EGL_BAD_NATIVE_WINDOW"
            else -> "Unknown error"
        } + " ($errorCode)"
}
