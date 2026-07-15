// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';
import 'package:flutter_svg/svg.dart';

/// An icon from an image asset (either a PNG or an SVG).
class AssetIcon extends StatelessWidget {
  const AssetIcon({
    Key? key,
    required this.path,
    this.size,
    this.width,
    this.height,
    this.color,
    this.fit,
  })  : assert(
          size == null || (width == null && height == null),
          'size parameter overrides width and height parameters',
        ),
        super(key: key);

  /// The path of the asset. Must be an image or an SVG.
  final String path;

  /// If provided, use this as both the [width] and the [height].
  /// If no size (including [width] and [height]) are provided, this will use the default font size for its context.
  final double? size;

  /// The width of the icon.
  final double? width;

  /// The height of the icon.
  final double? height;

  /// The color of the icon.
  final Color? color;

  /// How to inscribe the image into the space allocated during layout.
  final BoxFit? fit;

  @override
  Widget build(BuildContext context) {
    final double? effectiveSize;
    if (size == null && width == null && height == null) {
      effectiveSize = DefaultTextStyle.of(context).style.fontSize;
    } else {
      effectiveSize = size;
    }

    if (path.endsWith('.svg')) {
      return SvgPicture.asset(
        path,
        width: effectiveSize ?? width,
        height: effectiveSize ?? height,
        color: color,
        colorBlendMode: BlendMode.modulate,
        fit: fit ?? BoxFit.contain,
      );
    } else {
      return Image.asset(
        path,
        width: effectiveSize ?? width,
        height: effectiveSize ?? height,
        color: color,
        colorBlendMode: BlendMode.modulate,
        fit: fit,
      );
    }
  }
}
