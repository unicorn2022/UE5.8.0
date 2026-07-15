// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:collection';
import 'dart:io';

import 'package:device_info_plus/device_info_plus.dart';
import 'package:flutter/widgets.dart';
import 'package:logging/logging.dart';
import 'package:permission_handler/permission_handler.dart';

import '../platform/tentacle_api.dart';
import '../platform/tentacle_api.g.dart';
import 'device.dart';

final _log = Logger('TentacleManager');

/// How often to poll for Tentacle device information from the native API.
const _deviceInfoPollInterval = Duration(seconds: 1);

/// Sets up Bluetooth Low Energy and makes calls to the native Tentacle API in order to keep cached info about any
/// detected Tentacle devices.
class TentacleDeviceManager with WidgetsBindingObserver {
  TentacleDeviceManager() {
    WidgetsBinding.instance.addObserver(this);

    _devicesStream
      ..onListen = _onDevicesStreamListen
      ..onCancel = _onDevicesStreamCancel;
  }

  /// Timer used to poll the native API.
  Timer? _pollTimer;

  /// Whether we're currently scanning (or trying to).
  bool _bIsScanning = false;

  /// A stream which broadcasts whenever cached device information updates.
  final _devicesStream = StreamController<UnmodifiableListView<TentacleDevice>>.broadcast();

  /// Map from native index to associated Tentacle device.
  final Map<int, TentacleDevice> _devicesByNativeIndex = {};

  /// Map from device identifier to associated Tentacle device.
  final Map<String, TentacleDevice> _devicesByIdentifier = {};

  /// Map from device identifier to completer that will complete when the device is found.
  final Map<String, Completer<TentacleDevice>> _deviceCompletersByIdentifier = {};

  /// Last received list of cached device information, where array index is the native ID.
  final List<TentacleDeviceInfo> _cachedInfo = [];

  /// Whether scanning for Tentacle devices is possible.
  bool _bCanScan = true;

  /// Completes once the pending Tentacle scan startup process is finished.
  /// Null if the Tentacle scan is not currently pending startup.
  Future? _pendingScanStart;

  /// A stream which broadcasts whenever the cached list of devices updates.
  /// The device manager will only scan as long as there is at least one listener to this stream.
  Stream<UnmodifiableListView<TentacleDevice>> get devicesStream => _devicesStream.stream;

  /// A list of devices for which we have cached information.
  List<TentacleDevice> get devices => _devicesByNativeIndex.values.toList(growable: false);

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    switch (state) {
      case AppLifecycleState.resumed:
        _startScanning();
        break;

      case AppLifecycleState.paused:
        _stopScanning();
        break;

      default:
        break;
    }
  }

  /// Release any resources held by this manager.
  void dispose() {
    _pollTimer?.cancel();
    _devicesStream.close();
    WidgetsBinding.instance.removeObserver(this);
  }

  /// Get a device by its unique [identifier] string.
  TentacleDevice? getDeviceByIdentifier(String identifier) {
    return _devicesByIdentifier[identifier];
  }

  /// Returns a future that will complete with the device associated with a unique [identifier] string as soon as it is
  /// detected by the manager.
  Future<TentacleDevice?> getFutureDeviceByIdentifier(String identifier) {
    if (identifier.isEmpty) {
      return Future.value(null);
    }

    final TentacleDevice? device = getDeviceByIdentifier(identifier);
    if (device != null) {
      return Future.value(device);
    }

    return _deviceCompletersByIdentifier
        .putIfAbsent(
          identifier,
          () => Completer<TentacleDevice>(),
        )
        .future;
  }

  /// Get the cached information for the device with the given [nativeId].
  TentacleDeviceInfo getDeviceInfo(int nativeId) {
    assert(nativeId >= 0);
    assert(nativeId < _cachedInfo.length);

    return _cachedInfo[nativeId];
  }

  /// Start scanning for Tentacle devices.
  /// This should not be called until the adapter is ready.
  void _startScanning() {
    if (_bIsScanning || !_bCanScan) {
      return;
    }

    _bIsScanning = true;

    // Don't run the startup process again if we're already waiting to scan
    _pendingScanStart ??= _startScanningAsync();
  }

  /// Asynchronous method to start up Tentacle scan.
  Future _startScanningAsync() async {
    final PermissionStatus permissionResult = await _requestPermissions();
    if (permissionResult != PermissionStatus.granted) {
      _log.warning('Bluetooth permission was not granted ($permissionResult). Tentacle scan will not run.');

      _bCanScan = false;
      _stopScanning();
      return;
    }

    if (!_bIsScanning) {
      // Scan was cancelled before permissions were granted
      _stopScanning();
      return;
    }

    _pollTimer = Timer.periodic(_deviceInfoPollInterval, (timer) => _updateDeviceInfo());

    _log.info('Started Bluetooth scan for Tentacle devices');
    TentacleApi.instance.host.startScanning();

    _pendingScanStart = null;
  }

  /// Stop scanning for Tentacle devices.
  void _stopScanning() async {
    if (!_bIsScanning) {
      return;
    }

    _bIsScanning = false;

    // None of the below will have started until the full scan startup is finished, so if it isn't, just unset the flag.
    // This prevents us from repeatedly starting/stopping the scan and requesting permissions while widgets are being
    // created and destroyed.
    if (_pendingScanStart == null) {
      TentacleApi.instance.host.stopScanning();
      _pollTimer?.cancel();
      _log.info('Stopped Bluetooth scan for Tentacle devices');
    }
  }

  /// Request the necessary permission for Bluetooth scanning.
  Future<PermissionStatus> _requestPermissions() async {
    late final Permission permission;

    _log.info('Requesting Bluetooth permission for Tentacle scan');

    if (Platform.isAndroid) {
      // Android always allows Bluetooth, but has more granular permissions for BLE
      final deviceInfo = await DeviceInfoPlugin().androidInfo;
      if (deviceInfo.version.sdkInt >= 31) {
        permission = Permission.bluetoothScan;
      } else {
        // Older Android APIs don't have a scanning-specific permission and instead require location
        permission = Permission.locationWhenInUse;
      }
    } else {
      // General Bluetooth permission must be requested on iOS
      permission = Permission.bluetooth;
    }

    final result = await permission.request();

    _log.info('Got Bluetooth permission result: $result');

    return result;
  }

  /// Request the latest device information from the native API and cache it.
  void _updateDeviceInfo() async {
    final List<TentacleDeviceInfo?> newInfo = await TentacleApi.instance.host.getCachedDeviceInfo();

    // Update cache and create devices for new native IDs.
    // Note: the Tentacle API maintains a cache even for devices that are no longer detected (unless the cache is
    // explicitly cleared), so we don't delete devices here.
    for (final (int nativeIndex, TentacleDeviceInfo? info) in newInfo.indexed) {
      assert(info != null); // Pigeon always generates nullable types for structs, but this should never be null

      if (nativeIndex >= _cachedInfo.length) {
        _cachedInfo.add(info!);
      } else {
        _cachedInfo[nativeIndex] = info!;
      }

      TentacleDevice? device = _devicesByNativeIndex[nativeIndex];

      if (device == null) {
        // New device detected, so create a local handle
        device = TentacleDevice(nativeIndex: nativeIndex, manager: this);
        final String identifier = device.info.identifier;

        _devicesByNativeIndex[nativeIndex] = device;
        _devicesByIdentifier[identifier] = device;

        // Complete the future if one exists, removing it so we don't try to complete it again
        _deviceCompletersByIdentifier.remove(identifier)?.complete(device);
      }
    }

    _devicesStream.add(UnmodifiableListView(devices));
  }

  /// Called when at least one listener to [_devicesStream] begins listening.
  void _onDevicesStreamListen() {
    _startScanning();
  }

  /// Called when there are no more listeners to [_devicesStream].
  void _onDevicesStreamCancel() {
    _stopScanning();
  }
}
