// Copyright Epic Games, Inc. All Rights Reserved.

import 'timecode.dart';

/// Base for classes that provide a timecode synchronized to a source.
abstract class TimecodeSynchronizer {
  /// The current timecode.
  Timecode get timecode;

  /// Clean up any resources this used.
  void dispose();
}
