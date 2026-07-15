// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

import 'asset_icon.dart';

/// A checkbox button that matches the Epic app style.
class EpicCheckbox extends StatelessWidget {
  const EpicCheckbox({
    Key? key,
    required this.bChecked,
    this.label,
    this.onPressed,
  }) : super(key: key);

  /// If true, show a checkmark in the box.
  final bool bChecked;

  /// Label text to show next to the checkbox.
  final String? label;

  /// Callback function when this is pressed.
  final Function()? onPressed;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      child: GestureDetector(
        onTap: onPressed,
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            AssetIcon(
              size: 20,
              path: bChecked
                  ? 'packages/epic_common/assets/icons/checkbox_opaque_checked.svg'
                  : 'packages/epic_common/assets/icons/checkbox_unchecked.svg',
            ),
            if (label != null)
              Padding(
                padding: EdgeInsets.symmetric(horizontal: 8),
                child: Text(
                  label!,
                  style: Theme.of(context).textTheme.headlineSmall,
                ),
              ),
          ],
        ),
      ),
    );
  }
}
