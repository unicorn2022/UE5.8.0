// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.flutter_tentacle.util

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanRecord
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import androidx.core.content.ContextCompat.getSystemService
import com.tentaclesync.android.sdk.AdvertisementParser
import com.tentaclesync.android.sdk.swig.TentacleAdvertisement
import com.tentaclesync.android.sdk.swig.tentaclelib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** Number of nanoseconds in a second. */
private const val NANOS_PER_SECOND: Double = 1000.0 * 1000.0 * 1000.0

/**
 * How often to restart the BLE scan (in milliseconds).
 * After 30 minutes of scanning, Android will switch to "opportunistic" BLE scan mode (i.e. only return results
 * if another app requests a scan). We can avoid this by restarting the scan periodically.
 */
private const val SCAN_RESTART_INTERVAL: Long = 30 * 60 * 1000

/** Uses BLE (Bluetooth Low Energy) to scan for Tentacle devices and update the Tentacle SDK's cache. */
class TentacleScanner(
    /** Context in which this was created. */
    context: Context,
) {
    /** ID for Tentacle device batteries advertised via BLE. */
    private val batteryUuid = ParcelUuid.fromString("0000180f-0000-1000-8000-00805F9b34FB")

    /** ID for Tentacle devices advertised via BLE. */
    private val openTcuUuid = ParcelUuid.fromString("0000FDAC-0000-1000-8000-00805F9b34FB")

    /** Job used to restart the scanner periodically. */
    private var scanRestartJob: Job? = null

    /** Whether this is currently scanning. */
    private var bIsScanning: Boolean = false

    /** Coroutine scope used for the UI thread. */
    private val uiScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    /** Bluetooth adapter used to scan, if it exists. */
    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val manager = getSystemService(context, BluetoothManager::class.java)
        manager?.adapter
    }

    /**
     * Bluetooth scanner to use for BLE scanning.
     * Returns null if Bluetooth is not available and enabled.
     */
    private val bleScanner: BluetoothLeScanner?
        get() {
            if (bluetoothAdapter?.isEnabled != true) {
                return null
            }

            return bluetoothAdapter!!.bluetoothLeScanner
        }

    /** Callback object for BLE scan results. */
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            this@TentacleScanner.onScanResult(result)
        }
    }

    /** Start scanning for Tentacle devices. */
    @SuppressLint("MissingPermission") // False positive
    fun startScan() {
        if (bleScanner == null) {
            return
        }

        if (bIsScanning) {
            // Stop the scan first so we start a fresh one
            stopScan()
        }

        bIsScanning = true

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        val filters = listOf(
            ScanFilter.Builder().setServiceUuid(openTcuUuid).build(),
            ScanFilter.Builder().setServiceUuid(batteryUuid).build(),
        )

        bleScanner!!.startScan(filters, settings, scanCallback)

        scanRestartJob = uiScope.launch {
            while (true) {
                delay(SCAN_RESTART_INTERVAL)
                startScan()
            }
        }
    }

    /** Stop scanning for Tentacle devices. */
    @SuppressLint("MissingPermission") // False positive
    fun stopScan() {
        bleScanner?.stopScan(scanCallback)
        scanRestartJob?.cancel()
        bIsScanning = false
    }

    /** Called when a scan result is received from the BLE scanner. */
    private fun onScanResult(result: ScanResult?) {
        if (result == null) {
            return
        }

        val record: ScanRecord = result.scanRecord ?: return

        val advertisementList: Map<Int, ByteArray> = AdvertisementParser.parseAdvertisementBytes(record.bytes)
        val manufacturerData: ByteArray = AdvertisementParser.getManufacturerSpecificData(advertisementList)
        val serviceData: ByteArray = AdvertisementParser.getServiceData(advertisementList)

        val timestamp: Double = result.timestampNanos / NANOS_PER_SECOND

        val nameBytes: ByteArray = (record.deviceName ?: "").toByteArray()
        val identifierBytes: ByteArray = result.device.address.toByteArray()

        val advertisement: TentacleAdvertisement = tentaclelib.TentacleAdvertisementInit(
            manufacturerData,
            manufacturerData.size,
            serviceData,
            serviceData.size,
            result.rssi.toByte(),
            timestamp,
            identifierBytes,
            identifierBytes.size,
            nameBytes,
            nameBytes.size,
        )

        if (advertisement.valid) {
            tentaclelib.TentacleDeviceCacheProcess(advertisement)
        }
    }
}
