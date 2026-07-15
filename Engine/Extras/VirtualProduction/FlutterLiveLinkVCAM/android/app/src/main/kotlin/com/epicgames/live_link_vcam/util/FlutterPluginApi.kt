// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.util

import kotlinx.coroutines.*

/**
 * Interface for [FlutterPluginApi], allowing other classes to implement it by delegation.
 */
interface IFlutterPluginApi<T> {
    /** Scope used for coroutines handling work off the main thread. */
    val workCoroutineScope: CoroutineScope

    /** Scope used for coroutines that call back to Flutter via the UI thread. */
    val uiCoroutineScope: CoroutineScope

    /**
     * Call a function in the Flutter API.
     * The call will be dispatched on the UI thread (as required).
     * @param call A function taking the Flutter API and calling into it as needed.
     */
    fun callFlutter(call: (T) -> Unit)

    /**
     * Launch a coroutine to perform some work, then call a callback with the result on the UI thread.
     * This helper function makes it easier to respond to messages from the Flutter API, which always have to call back
     * on the UI thread after performing some work.
     *
     * @param callback The callback function to call when the work is completed.
     * @param work A suspend function that performs work and returns a result.
     */
    fun <T> doWorkAndRespondOnUiThread(
        callback: (Result<T>) -> Unit,
        work: suspend () -> Result<T>,
    )

    /**
     * Cancel all running coroutines.
     */
    fun cancelCoroutines()
}

/**
 * Base class for all APIs that communicates to and from a Flutter plugin.
 */
class FlutterPluginApi<T>(
    /** The API used to send messages to Flutter. */
    val flutter: T
) : IFlutterPluginApi<T> {
    override val workCoroutineScope = CoroutineScope(SupervisorJob())

    override val uiCoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    override fun callFlutter(call: (T) -> Unit) {
        uiCoroutineScope.launch {
            call(flutter)
        }
    }

    override fun <T> doWorkAndRespondOnUiThread(
        callback: (Result<T>) -> Unit,
        work: suspend () -> Result<T>,
    ) {
        workCoroutineScope.launch {
            val result: Result<T> = work()

            uiCoroutineScope.launch {
                callback(result)
            }
        }
    }

    override fun cancelCoroutines() {
        workCoroutineScope.cancel()
        uiCoroutineScope.cancel()
    }
}
