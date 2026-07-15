// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:ffi';

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';
import 'package:logging/logging.dart';
import 'package:system_clock/system_clock.dart';

import '../models/device.dart';
import 'tentacle_api.g.dart';
import 'tentacle_ffi.g.dart' as tffi;

final _log = Logger('TentacleFfi');

/// API for communication with the native platform's Tentacle API. This API uses both a Pigeon platform channel and an
/// FFI library binding.
///
/// The Pigeon API is used to call into Tentacle's Kotlin/Swift layer, which uses the platform's Bluetooth features,
/// updates the C library's cache, and returns cached data. Because they use a platform channel, these methods are
/// handled via asynchronous messaging with inherent latency, so they're used for non-time-sensitive data.
///
/// The FFI binding ([_ffiLibrary]) calls directly into the C library synchronously, allowing for low overhead access to
/// time-sensitive information such as the current timecode.
class TentacleApi extends TentacleFlutterApi {
  /// Private constructor.
  TentacleApi._() : host = TentacleHostApi() {
    TentacleFlutterApi.setUp(this);
    _bindFfiLibrary();
  }

  /// Instance of the singleton API.
  static final TentacleApi _instance = TentacleApi._();

  /// Instance of the singleton API.
  static TentacleApi get instance => _instance;

  /// The API that communicates with the host's native plugin via a Pigeon platform channel.
  final TentacleHostApi host;

  /// The loaded Tentacle dynamic library.
  DynamicLibrary? _dynamicLibrary;

  /// The native library bound using FFI.
  tffi.TentacleLibrary? _ffiLibrary;

  /// A pointer to C memory we reuse to store the timecode when it's requested.
  Pointer<tffi.TentacleTimecode>? _timecodePtr;

  /// Get the number of seconds used by Tentacle to calculate timecodes.
  double get timestamp {
    final Duration time = switch (defaultTargetPlatform) {
      // On iOS, Tentacle uses mach_absolute_time, which is the uptime (not counting time while device is asleep)
      TargetPlatform.iOS => SystemClock.uptime(),

      // On Android, Tentacle uses the system's real time since boot
      TargetPlatform.android => SystemClock.elapsedRealtime(),
      _ => SystemClock.elapsedRealtime(),
    };
    return time.inMicroseconds / Duration.microsecondsPerSecond;
  }

  /// Get the current timecode represented as seconds for a Tentacle [device].
  double getDeviceTimecodeSeconds(TentacleDevice device) {
    if (_ffiLibrary == null) {
      return 0;
    }

    if (_timecodePtr == null) {
      _timecodePtr = malloc.allocate<tffi.TentacleTimecode>(sizeOf<tffi.TentacleTimecode>());
    }

    final tffi.TentacleDevice nativeDevice = _ffiLibrary!.TentacleDeviceCacheGetDevice(device.nativeIndex);
    _timecodePtr!.ref = nativeDevice.timecode;

    final double timecodeSeconds = _ffiLibrary!.TentacleTimecodeSecondsAtTimestamp(
      _timecodePtr!,
      device.info.frameRate,
      nativeDevice.advertisement.dropFrame,
      timestamp,
    );

    return timecodeSeconds;
  }

  /// Free any resources and close the native library.
  void dispose() {
    if (_timecodePtr != null) {
      malloc.free(_timecodePtr!);
    }

    _dynamicLibrary?.close();
    _ffiLibrary = null;
  }

  /// Bind to the Tentacle C library using FFI.
  void _bindFfiLibrary() {
    try {
      final libName = switch (defaultTargetPlatform) {
        TargetPlatform.android => 'libtentacle-lib.so',
        TargetPlatform.iOS => 'Tentacle.framework/Tentacle',
        _ => throw Exception('Unsupported platform for Tentacle')
      };

      _dynamicLibrary = DynamicLibrary.open(libName);
      _ffiLibrary = tffi.TentacleLibrary(_dynamicLibrary!);
    } catch (error, stack) {
      _log.severe('Failed to load native Tentacle library. Tentacle timecodes will be unavailable.', error, stack);
    }
  }
}
