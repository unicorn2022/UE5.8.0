// Copyright Epic Games, Inc. All Rights Reserved.

import 'native_object.dart';

/// A [NativeObject] which is owned (i.e. created and disposed of) by the native plugin.
abstract class NativeOwnedObject extends NativeObject {
  NativeOwnedObject(int nativeId) : _nativeId = nativeId;

  /// The ID sent from the native WebRTC plugin.
  final int _nativeId;

  @override
  Future<int> internalInit() => Future.value(_nativeId);

  @override
  Future<void> internalDispose() async {}
}
