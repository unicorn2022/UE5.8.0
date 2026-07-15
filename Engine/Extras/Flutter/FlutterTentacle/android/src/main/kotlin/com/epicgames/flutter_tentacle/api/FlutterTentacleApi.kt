// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.flutter_tentacle.api

import TentacleDeviceInfo
import TentacleFlutterApi
import TentacleHostApi
import TentacleProductId
import android.content.Context
import com.epicgames.flutter_tentacle.util.FlutterPluginApi
import com.epicgames.flutter_tentacle.util.IFlutterPluginApi
import com.epicgames.flutter_tentacle.util.TentacleScanner
import com.tentaclesync.android.sdk.swig.TentacleAdvertisement
import com.tentaclesync.android.sdk.swig.TentacleDevice
import com.tentaclesync.android.sdk.swig.tentaclelib
import io.flutter.plugin.common.BinaryMessenger
import com.tentaclesync.android.sdk.swig.TentacleProductId as NativeTentacleProductId

/**
 * Handles messages about Tentacle devices to and from the Flutter API.
 */
class FlutterTentacleApi(
    /** Messenger used to communicate with Flutter. */
    binaryMessenger: BinaryMessenger,

    /** Context in which this was created. */
    context: Context,
) : TentacleHostApi, IFlutterPluginApi<TentacleFlutterApi> by FlutterPluginApi(TentacleFlutterApi(binaryMessenger)) {
    /**
     * The application context in which this was created.
     * Note that this is guaranteed to survive the entire application's existence (as opposed to the passed-in context,
     * which may only be tied to e.g. an activity), so this avoids a memory leak.
     */
    private val applicationContext = context.applicationContext

    /** Scans for Tentacle devices using BLE. */
    private val scanner = TentacleScanner(applicationContext)

    /** Set up messaging with Flutter. */
    init {
        TentacleHostApi.setUp(binaryMessenger, this)
    }

    //region TentacleHostApi
    override fun startScanning() {
        scanner.startScan()
    }

    override fun stopScanning() {
        scanner.stopScan()
    }

    override fun getCachedDeviceInfo(): List<TentacleDeviceInfo> {
        val devices = mutableListOf<TentacleDeviceInfo>()

        val deviceCount: Int = tentaclelib.TentacleDeviceCacheGetSize()
        for (deviceIndex in 0 until deviceCount) {
            val device: TentacleDevice = tentaclelib.TentacleDeviceCacheGetDevice(deviceIndex)
            val advertisement: TentacleAdvertisement = device.advertisement

            devices += TentacleDeviceInfo(
                advertisement.greenMode,
                advertisement.charging,
                advertisement.dropFrame,
                advertisement.battery.toLong(),
                tentaclelib.TentacleAdvertisementGetFrameRate(advertisement),
                advertisement.timecode.receivedTimestamp,
                advertisement.icon.toLong(),
                tentaclelib.TentacleDeviceGetRssi(device).toLong(),
                tentacleProductIdToFlutter(advertisement.productId),
                advertisement.identifier,
                advertisement.name,
            )
        }

        return devices
    }

    /** Convert from the native Tentacle device type to the Flutter equivalent. */
    private fun tentacleProductIdToFlutter(productId: Int): TentacleProductId = when (productId) {
        NativeTentacleProductId.TentacleProductIdGeneric -> TentacleProductId.GENERIC
        NativeTentacleProductId.TentacleProductIdSyncE -> TentacleProductId.SYNC_E
        NativeTentacleProductId.TentacleProductIdTrackE -> TentacleProductId.TRACK_E
        else -> TentacleProductId.GENERIC
    }

    //endregion
}
