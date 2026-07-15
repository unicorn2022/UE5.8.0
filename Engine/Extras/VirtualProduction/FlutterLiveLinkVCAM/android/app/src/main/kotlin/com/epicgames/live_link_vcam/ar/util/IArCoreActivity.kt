// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.ar.util

/** Signature for a callback function that handles responses to ARCore activity requests. */
typealias ArCoreActivityRequestCallback = (Result<Unit>) -> Unit

/** Signature for a callback function that handles ARCore permission requests. */
typealias ArCorePermissionCallback = (Result<Unit>) -> Unit

/** Interface for an activity that supports ARCore functionality */
interface IArCoreActivity {
    /** Request install of the ARCore package if necessary. */
    fun requestInstall(callback: ArCoreActivityRequestCallback)

    /** Request camera permission required to use ARCore if necessary. */
    fun requestCameraPermission(callback: ArCorePermissionCallback)
}
