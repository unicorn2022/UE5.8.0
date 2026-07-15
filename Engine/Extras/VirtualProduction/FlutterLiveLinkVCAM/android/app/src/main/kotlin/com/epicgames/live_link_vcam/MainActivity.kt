// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam

import android.Manifest
import android.content.pm.PackageManager
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.core.app.ActivityCompat
import com.epicgames.live_link_vcam.ar.api.FlutterArSessionApi
import com.epicgames.live_link_vcam.ar.util.ArCoreActivityRequestCallback
import com.epicgames.live_link_vcam.ar.util.IArCoreActivity
import com.epicgames.live_link_vcam.gamepad.api.FlutterGamepadApi
import com.epicgames.live_link_vcam.util.FlutterPluginBindingProvider
import com.epicgames.live_link_vcam.webrtc.api.FlutterRtcDataChannelApi
import com.epicgames.live_link_vcam.webrtc.api.FlutterRtcPeerConnectionApi
import com.epicgames.live_link_vcam.webrtc.api.FlutterRtcVideoViewControllerApi
import com.google.ar.core.ArCoreApk
import com.google.ar.core.ArCoreApk.InstallStatus
import com.google.ar.core.exceptions.UnavailableDeviceNotCompatibleException
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.BinaryMessenger
import io.flutter.view.TextureRegistry
import org.webrtc.EglBase

/** Arbitrary code used to indicate a request for camera permissions */
private const val CODE_REQUEST_CAMERA: Int = 100

class MainActivity : FlutterActivity(), IArCoreActivity {
    companion object {
        /** Context used to render video streams. */
        val eglBaseContext: EglBase.Context by lazy {
            EglBase.create().eglBaseContext
        }

        /** API that handles FlutterPeerConnection instances. */
        lateinit var peerConnectionApi: FlutterRtcPeerConnectionApi
            private set

        /** API that handles FlutterDataChannel instances. */
        lateinit var dataChannelApi: FlutterRtcDataChannelApi
            private set

        /** API that handles FlutterVideoViewController instances. */
        lateinit var videoViewControllerApi: FlutterRtcVideoViewControllerApi
            private set

        /** API that handles FlutterArSession instances. */
        lateinit var arSessionApi: FlutterArSessionApi
            private set

        /** API that handles gamepad input. */
        lateinit var gamepadApi: FlutterGamepadApi
            private set

        /**
         * Texture registry to use for sharing textures with Flutter.
         */
        lateinit var textureRegistry: TextureRegistry
            private set
    }

    /** Callback to call when ARCore installation completes or fails. */
    private var arCoreActivityRequestCallback: ArCoreActivityRequestCallback? = null

    /** Callback to call when ARCore request for camera permission completes or fails. */
    private var arCoreCameraPermissionCallback: ArCoreActivityRequestCallback? = null

    /**
     * Exposes the Flutter plugin binding for access to e.g. its texture binding facilities.
     */
    private val flutterPluginBindingProvider = FlutterPluginBindingProvider()

    override fun dispatchGenericMotionEvent(event: MotionEvent?): Boolean {
        if (gamepadApi.dispatchGenericMotionEvent(event)) {
            return true
        }

        return super.dispatchGenericMotionEvent(event)
    }

    override fun dispatchKeyEvent(event: KeyEvent?): Boolean {
        if (gamepadApi.dispatchKeyEvent(event)) {
            return true
        }

        return super.dispatchKeyEvent(event)
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        flutterEngine.plugins.add(flutterPluginBindingProvider)

        super.configureFlutterEngine(flutterEngine)

        val binaryMessenger: BinaryMessenger = flutterEngine.dartExecutor.binaryMessenger

        peerConnectionApi = FlutterRtcPeerConnectionApi(binaryMessenger, context)
        dataChannelApi = FlutterRtcDataChannelApi(binaryMessenger)
        videoViewControllerApi =
            FlutterRtcVideoViewControllerApi(binaryMessenger)
        arSessionApi = FlutterArSessionApi(binaryMessenger, context, this)
        gamepadApi = FlutterGamepadApi(binaryMessenger, context)
        textureRegistry = flutterPluginBindingProvider.binding!!.textureRegistry
    }

    override fun onResume() {
        if (arCoreActivityRequestCallback != null) {
            // We were waiting for the activity to resume after requesting that the user install ARCore.
            // This will immediately return a result or throw an exception when passing false for userRequestedInstall,
            // so either way we can call back immediately.
            val result: Result<Unit> = try {
                ArCoreApk.getInstance().requestInstall(this, false)
                Result.success(Unit)
            } catch (error: Exception) {
                Result.failure(error)
            }

            arCoreActivityRequestCallback!!(result)
            arCoreActivityRequestCallback = null
        }

        super.onResume()
    }

    override fun onDestroy() {
        peerConnectionApi.disposeAll()
        videoViewControllerApi.dispose()

        super.onDestroy()
    }

    //region IArCoreInstallProvider
    override fun requestInstall(callback: ArCoreActivityRequestCallback) {
        if (arCoreActivityRequestCallback != null) {
            return
        }

        // Early out if ARCore is entirely unsupported on this device
        if (!ArCoreApk.getInstance().checkAvailability(this).isSupported) {
            callback(Result.failure(UnavailableDeviceNotCompatibleException()))
            return
        }

        arCoreActivityRequestCallback = callback

        // If uninstalled, this will pause the activity and resume after the user has installed or declined
        val status: InstallStatus = ArCoreApk.getInstance()
            .requestInstall(
                this,
                true,
                ArCoreApk.InstallBehavior.REQUIRED,
                ArCoreApk.UserMessageType.APPLICATION
            )

        if (status == InstallStatus.INSTALLED) {
            // ARCore was already installed, so we call back immediately and clear the callback
            arCoreActivityRequestCallback = null
            callback(Result.success(Unit))
        }
    }

    override fun requestCameraPermission(callback: ArCoreActivityRequestCallback) {
        if (arCoreCameraPermissionCallback != null) {
            return
        }

        if (ActivityCompat.checkSelfPermission(
                this,
                Manifest.permission.CAMERA
            ) == PackageManager.PERMISSION_GRANTED
        ) {
            // Permission already granted
            callback(Result.success(Unit))
            return
        }

        arCoreCameraPermissionCallback = callback

        ActivityCompat.requestPermissions(
            this,
            arrayOf(Manifest.permission.CAMERA),
            CODE_REQUEST_CAMERA
        )
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)

        if (requestCode != CODE_REQUEST_CAMERA || arCoreCameraPermissionCallback == null) {
            return
        }

        val granted: Boolean = grantResults.all { it == PackageManager.PERMISSION_GRANTED }

        if (granted) {
            arCoreCameraPermissionCallback!!.invoke(Result.success(Unit))
        } else {
            arCoreCameraPermissionCallback!!.invoke(Result.failure(Exception("Permission declined")))
        }

        arCoreCameraPermissionCallback = null
    }
    //endregion
}
