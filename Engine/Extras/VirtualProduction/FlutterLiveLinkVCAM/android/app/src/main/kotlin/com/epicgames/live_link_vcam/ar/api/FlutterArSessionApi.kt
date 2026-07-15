// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.ar.api

import ArAvailability
import ArSessionFlutterApi
import ArSessionHostApi
import android.content.Context
import com.epicgames.live_link_vcam.ar.flutter.FlutterArSession
import com.epicgames.live_link_vcam.ar.util.IArCoreActivity
import com.epicgames.live_link_vcam.util.FlutterPluginApi
import com.epicgames.live_link_vcam.util.IFlutterPluginApi
import com.epicgames.live_link_vcam.util.IdObjectManager
import com.google.ar.core.exceptions.UnavailableUserDeclinedInstallationException
import io.flutter.plugin.common.BinaryMessenger
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

class FlutterArSessionApi(
    /** Messenger used to communicate with Flutter. */
    binaryMessenger: BinaryMessenger,

    /** Context in which this was created. */
    context: Context,

    /** The provider of ARCore availability/install functionality. */
    private val arCoreAvailabilityProvider: IArCoreActivity,
) : ArSessionHostApi, IFlutterPluginApi<ArSessionFlutterApi> by FlutterPluginApi(ArSessionFlutterApi(binaryMessenger)) {
    /**
     * The application context in which this was created.
     * Note that this is guaranteed to survive the entire application's existence (as opposed to the passed-in context,
     * which may only be tied to e.g. an activity), so this avoids a memory leak.
     */
    private val applicationContext = context.applicationContext

    /** Manager for indexed AR sessions shared with Flutter. */
    private val sessionManager by lazy {
        object : IdObjectManager<FlutterArSession>("ARSession") {}
    }

    /** Set up messaging with Flutter. */
    init {
        ArSessionHostApi.setUp(binaryMessenger, this)
    }

    //region ArSessionHostApi
    override fun initialize(callback: (Result<ArAvailability>) -> Unit) {
        doWorkAndRespondOnUiThread(callback) work@{
            // Request install
            val installResult: Result<Unit> = suspendCoroutine { continuation ->
                arCoreAvailabilityProvider.requestInstall { result ->
                    continuation.resume(result)
                }
            }

            if (installResult.isFailure) {
                // Bail out if install failed or was declined
                return@work Result.success(
                    when (installResult.exceptionOrNull()) {
                        is UnavailableUserDeclinedInstallationException -> ArAvailability.NOTINSTALLED
                        else -> ArAvailability.NOTSUPPORTED
                    }
                )
            }

            // Request camera permission
            val permissionResult: Result<Unit> = suspendCoroutine { continuation ->
                arCoreAvailabilityProvider.requestCameraPermission { result ->
                    continuation.resume(result)
                }
            }

            if (permissionResult.isFailure) {
                // Bail out if permission wasn't granted
                return@work Result.success(ArAvailability.CAMERANOTPERMITTED)
            }

            return@work Result.success(ArAvailability.AVAILABLE)
        }
    }

    override fun create(callback: (Result<Long>) -> Unit) {
        sessionManager.register(
            FlutterArSession(
                api = this,
                context = applicationContext,
                initCallback = callback
            )
        )
    }

    override fun dispose(sessionId: Long) {
        sessionManager.unregister(sessionId)
    }

    override fun run(sessionId: Long) {
        sessionManager.getChecked(sessionId).run()
    }

    override fun pause(sessionId: Long) {
        sessionManager.getChecked(sessionId).pause()
    }
    // endregion
}
