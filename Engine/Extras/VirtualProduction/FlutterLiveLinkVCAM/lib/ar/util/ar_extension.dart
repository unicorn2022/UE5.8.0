// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:vector_math/vector_math.dart';

import '../api/ar_api.g.dart';

extension ArFrameExtension on ArFrame {
  /// The 4x4 matrix representation of the tracked camera's transform.
  Matrix4 get cameraTransform => Matrix4.fromBuffer(cameraTransformData.buffer, cameraTransformData.offsetInBytes);
}
