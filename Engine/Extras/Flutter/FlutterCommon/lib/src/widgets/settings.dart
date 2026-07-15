// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

import '../../../localizations.dart';
import '../../theme.dart';
import '../../widgets.dart';

/// Outer scaffold to use for all settings pages.
class SettingsPageScaffold extends StatelessWidget {
  const SettingsPageScaffold({
    Key? key,
    required this.title,
    required this.body,
    this.titleBarTrailing,
    this.bWrapBodyInScrollView = true,
  }) : super(key: key);

  /// The title of the page.
  final String title;

  /// The main contents of the page.
  final Widget body;

  /// Widget to add trailing the title bar.
  final Widget? titleBarTrailing;

  /// If true, wrap the body in a scroll view. If false, the body must handle overflow gracefully.
  /// This is useful if the body has its own scrolling behaviour (e.g. a builder-based ListView, which needs to control
  /// its own scrolling in order to render only the visible items).
  final bool bWrapBodyInScrollView;

  @override
  Widget build(BuildContext context) {
    final bool bCanPop = Navigator.of(context).canPop();

    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Container(
          height: 40,
          color: Theme.of(context).colorScheme.surfaceTint,
          padding: const EdgeInsets.symmetric(horizontal: 4),
          child: Stack(
            alignment: Alignment.center,
            clipBehavior: Clip.none,
            children: [
              SizedBox(
                width: 400,
                child: Text(
                  title,
                  style: Theme.of(context).textTheme.headlineSmall!.copyWith(color: UnrealColors.white),
                  overflow: TextOverflow.ellipsis,
                  textAlign: TextAlign.center,
                ),
              ),
              if (titleBarTrailing != null)
                Positioned(
                  right: 0,
                  child: titleBarTrailing!,
                ),
              Positioned(
                left: 0,
                child: bCanPop
                    ? EpicIconButton(
                        iconPath: 'packages/epic_common/assets/icons/chevron_left.svg',
                        onPressed: () => Navigator.of(context).maybePop(),
                      )
                    : EpicIconButton(
                        iconPath: 'packages/epic_common/assets/icons/close.svg',
                        onPressed: () => Navigator.of(context, rootNavigator: true).maybePop(),
                      ),
              ),
            ],
          ),
        ),
        const SettingsMenuDivider(),
        Expanded(
          child: bWrapBodyInScrollView ? EpicScrollView(child: body) : body,
        ),
      ],
    );
  }
}

/// An item in a [SettingsDialog] menu.
class SettingsMenuItem extends StatelessWidget {
  const SettingsMenuItem({
    Key? key,
    required this.title,
    this.iconPath,
    this.onTap,
    this.trailingIconPath = 'packages/epic_common/assets/icons/chevron_right.svg',
    this.leading,
    this.trailing,
    this.trailingIconPadding = 16,
    this.bAlwaysPadForLeading = true,
    this.bAlwaysPadForTrailingIcon = false,
    this.titleOverflow = TextOverflow.ellipsis,
    this.titleSoftWrap = false,
  })  : assert(leading == null || iconPath == null),
        super(key: key);

  /// The title to display for this item.
  final String title;

  /// The widget to display before the title, or null to leave an empty space (if [bAlwaysPadForLeading] is true).
  /// If set, [iconPath] must be null.
  final Widget? leading;

  /// Shorthand to use an icon as the [leading] widget by passing its path.
  /// If set, [leading] must be null.
  final String? iconPath;

  /// Function to call when the user taps on this item.
  final void Function()? onTap;

  /// Path of the icon to show trailing all content.
  final String? trailingIconPath;

  /// An extra widget to show before the right edge/trailing icon.
  final Widget? trailing;

  /// Padding to apply before the trailing icon (if present).
  final double trailingIconPadding;

  /// If true, always include space for the leading icon even if [leading] and [iconPath] are null.
  final bool bAlwaysPadForLeading;

  /// If true, always include space for the trailing icon even if [trailingIconPath] is null.
  final bool bAlwaysPadForTrailingIcon;

  /// How to handle the title text when it overflows the available space.
  final TextOverflow? titleOverflow;

  /// Whether the title text should soft wrap when out of space.
  final bool? titleSoftWrap;

  @override
  Widget build(BuildContext context) {
    final TextStyle textStyle = Theme.of(context).textTheme.labelMedium!;

    final double iconSize = 24;

    // Set up trailing widget
    final Widget? trailingIcon;
    if (trailingIconPath != null) {
      trailingIcon = AssetIcon(
        path: trailingIconPath!,
        size: iconSize,
      );
    } else if (bAlwaysPadForTrailingIcon) {
      trailingIcon = SizedBox.square(
        dimension: iconSize,
      );
    } else {
      trailingIcon = null;
    }

    // Set up leading widget
    Widget? leadingWidget = iconPath != null ? AssetIcon(path: iconPath!) : leading;
    if (leadingWidget != null || bAlwaysPadForLeading) {
      leadingWidget = Padding(
        padding: EdgeInsets.only(right: 16),
        child: SizedBox.square(
          dimension: iconSize,
          child: leadingWidget,
        ),
      );
    }

    return Semantics(
      button: true,
      child: MouseRegion(
        cursor: MaterialStateMouseCursor.clickable,
        child: GestureDetector(
          onTap: onTap,
          behavior: HitTestBehavior.opaque,
          child: SizedBox(
            height: 40,
            child: Padding(
              padding: EdgeInsets.symmetric(horizontal: 16),
              child: Row(
                children: [
                  if (leadingWidget != null) leadingWidget,
                  Expanded(
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Flexible(
                          child: Text(
                            title,
                            overflow: titleOverflow,
                            softWrap: titleSoftWrap,
                            style: textStyle,
                          ),
                        ),
                        if (trailing != null)
                          Padding(
                            padding: const EdgeInsets.only(left: 32),
                            child: DefaultTextStyle(
                              style: textStyle,
                              child: trailing!,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                      ],
                    ),
                  ),
                  if (trailingIcon != null)
                    Padding(
                      padding: EdgeInsets.only(left: trailingIconPadding),
                      child: trailingIcon,
                    ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// A divider shown within a [SettingsDialog] menu.
class SettingsMenuDivider extends StatelessWidget {
  const SettingsMenuDivider({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 2,
      color: Theme.of(context).colorScheme.surfaceVariant,
    );
  }
}

/// A button styled to indicate that it will disconnect the app from the engine.
class DisconnectButton extends StatelessWidget {
  const DisconnectButton({Key? key, required this.onTap}) : super(key: key);

  /// Called when the user taps the button.
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      button: true,
      child: MouseRegion(
        cursor: MaterialStateMouseCursor.clickable,
        child: GestureDetector(
          onTap: onTap,
          behavior: HitTestBehavior.opaque,
          child: Container(
            height: 36,
            padding: EdgeInsets.symmetric(horizontal: 24),
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(20),
              border: Border.all(
                width: 2,
                color: UnrealColors.warning,
              ),
            ),
            child: Center(
              child: Row(children: [
                AssetIcon(
                  path: 'packages/epic_common/assets/icons/exit.svg',
                  size: 24,
                  color: UnrealColors.warning,
                ),
                const SizedBox(width: 8),
                Text(
                  EpicCommonLocalizations.of(context)!.disconnectButtonLabel,
                  style: Theme.of(context).textTheme.headlineSmall!.copyWith(
                        color: UnrealColors.warning,
                      ),
                ),
              ]),
            ),
          ),
        ),
      ),
    );
  }
}
