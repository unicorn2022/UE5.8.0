// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

import '../../theme.dart';
import 'asset_icon.dart';

/// Widget for displaying text and an optional button when a pane has nothing else to show.
class EmptyPlaceholder extends StatelessWidget {
  const EmptyPlaceholder({Key? key, required this.message, this.button, this.iconPath}) : super(key: key);

  /// The main message to display.
  final String message;

  /// An optional button widget to show under the message.
  final Widget? button;

  /// Optional Icon to be shown as placeholder.
  final String? iconPath;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.all(16),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          AssetIcon(
            path: iconPath ?? 'packages/epic_common/assets/icons/unreal_u.svg',
            size: 64,
            color: UnrealColors.gray75,
          ),
          Flexible(
            child: const SizedBox(height: 32),
          ),
          Text(
            message,
            style: TextStyle(
              color: UnrealColors.gray56,
              fontStyle: FontStyle.italic,
              fontVariations: [FontVariation('wght', 400)],
              fontSize: 20,
              letterSpacing: 0.5,
              height: 1,
            ),
            textAlign: TextAlign.center,
          ),
          if (button != null) ...[
            Flexible(
              child: const SizedBox(height: 32),
            ),
            button!,
          ],
        ],
      ),
    );
  }
}
