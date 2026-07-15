// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

/// Align a canvas to the nearest pixel. This can prevent some shapes from rendering in ugly/lopsided ways.
void pixelAlignCanvas(Canvas canvas) {
  final canvasMatrix = Matrix4.fromFloat64List(canvas.getTransform());
  canvas.translate(0, -(canvasMatrix.getTranslation().y % 1));
}
