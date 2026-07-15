// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:flutter/foundation.dart';

import 'native_object_manager.dart';

/// An object which exists in the platform's native language.
/// By default, Flutter is responsible for managing (i.e. creating and disposing of) the native instance.
/// For objects that are managed by the native plugin, see [NativeOwnedObject].
abstract class NativeObject {
  /// ID for an object that doesn't exist.
  static int invalidId = -1;

  /// ID used to refer to the host's instance of this object.
  late final int _id;

  /// Completes when this is initialized.
  final _whenInitialized = Completer();

  /// Whether this has been disposed.
  bool _bDisposed = false;

  /// Whether this is currently being initialized.
  bool _bIsInitializing = false;

  /// Whether this is ready to be registered with the manager, so ID access is valid despite not being initialized.
  bool _bIsAwaitingRegistration = false;

  /// Completes when this is initialized.
  Future<void> get whenInitialized => _whenInitialized.future;

  /// Whether this has been initialized.
  bool get bInitialized => _whenInitialized.isCompleted;

  /// Whether this has been disposed.
  bool get bDisposed => _bDisposed;

  /// ID used to refer to the host's instance of this object.
  int get id {
    assert(bInitialized || _bIsAwaitingRegistration);
    return _id;
  }

  /// Manager responsible for this object's lifetime.
  @protected
  NativeObjectManager get manager;

  /// Initialize the native instance of this object.
  /// Must be called before using any other members of this object.
  @nonVirtual
  Future<void> initialize() async {
    if (_bIsInitializing) {
      return whenInitialized;
    }

    _bIsInitializing = true;

    assert(!bInitialized);

    _id = await internalInit();

    assert(_id != invalidId);

    _bIsAwaitingRegistration = true;
    manager.register(this);
    _bIsAwaitingRegistration = false;

    _whenInitialized.complete();
  }

  /// Dispose of this object in its native context, releasing any relevant resources.
  @nonVirtual
  Future<void> dispose() async {
    if (bDisposed) {
      return;
    }

    _bDisposed = true;

    await internalDispose();

    manager.unregister(this);
  }

  /// Class-specific implementation of [_init]. Returns the native ID once it's been created.
  @protected
  Future<int> internalInit();

  /// Class-specific implementation of [dispose]. The object is not removed from the manager until the Future completes.
  @protected
  Future<void> internalDispose();
}
