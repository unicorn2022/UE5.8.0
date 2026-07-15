// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:typed_data';

import '../api/webrtc_api.g.dart';

extension RtcDataBufferExtension on RtcDataBuffer {
  /// Helper function to get a ByteData view of the buffer's data.
  ByteData get byteData => ByteData.view(data.buffer, data.offsetInBytes);
}
