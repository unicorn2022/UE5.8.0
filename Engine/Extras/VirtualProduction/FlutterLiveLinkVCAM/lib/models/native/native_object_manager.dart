// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/foundation.dart';
import 'package:logging/logging.dart';

import 'native_object.dart';

final _log = Logger('NativeObjectManager');

/// Manages a type of [NativeObject] by maintaining a map from ID to object instance.
class NativeObjectManager<T extends NativeObject> {
  /// Map from host-provided ID to object instance.
  final Map<int, T> _nativeObjects = {};

  /// Start tracking a [nativeObject].
  void register(T nativeObject) {
    assert(!_nativeObjects.containsKey(nativeObject.id));

    _nativeObjects[nativeObject.id] = nativeObject;
  }

  /// Stop tracking a [nativeObject].
  void unregister(T nativeObject) {
    assert(_nativeObjects.containsKey(nativeObject.id));

    _nativeObjects.remove(nativeObject.id);
  }

  /// Get a managed native object by its unique [id] and confirm that it exists if appropriate.
  T? get(int id) {
    final T? nativeObject = _nativeObjects[id];

    if (nativeObject == null) {
      if (kReleaseMode) {
        throw Exception('Tried to access ${T.toString()} #$id, which does not exist');
      } else {
        _log.info(
          'Tried to access ${T.toString()} #$id, which does not exist.\n'
          'This may have been caused by a hot restart of the app, in which case this is safe to ignore',
        );
      }
    }

    return nativeObject;
  }
}
