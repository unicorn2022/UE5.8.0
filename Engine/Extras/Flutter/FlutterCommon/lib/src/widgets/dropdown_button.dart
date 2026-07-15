// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

import '../../theme.dart';
import 'asset_icon.dart';
import 'dropdown_list_menu.dart';
import 'list_menu.dart';

/// Possible states for a [ModalTextDropdownButton].
enum ModalDropdownButtonState {
  /// The modal is closed.
  closed,

  /// The modal is open and displaying under the button.
  openOnTop,

  /// The modal is open and displaying above the button.
  openOnBottom,
}

/// A button that uses a builder function to open a dropdown list modal when pressed.
class ModalDropdownButton<ItemType> extends StatefulWidget {
  const ModalDropdownButton({
    Key? key,
    required this.buttonBuilder,
    required this.menuBuilder,
    this.openRectExtraSpace = EdgeInsets.zero,
    this.bStretchDropdown = false,
    this.tooltipMessage,
    this.bDisabled = false,
  }) : super(key: key);

  /// Function that builds the button based on the dropdown's [state].
  final Widget Function(BuildContext context, ModalDropdownButtonState state) buttonBuilder;

  /// Function that builds the dropdown menu.
  /// Receives an [originTabBuilder] function which will build the menu's origin tab based on whether the tab [isOnTop].
  final Widget Function(BuildContext context, Widget Function(BuildContext, bool isOnTop) originTabBuilder) menuBuilder;

  /// How much space to add around the original button's bounding area when building its open version.
  final EdgeInsets openRectExtraSpace;

  /// Whether to stretch the dropdown to match the size of the button.
  final bool bStretchDropdown;

  /// An optional tooltip message to show when the button is hovered or long pressed.
  final String? tooltipMessage;

  /// whether the button is disabled or not.
  final bool? bDisabled;

  @override
  State<ModalDropdownButton<ItemType>> createState() => _ModalDropdownButtonState<ItemType>();
}

class _ModalDropdownButtonState<ItemType> extends State<ModalDropdownButton<ItemType>> {
  /// Key used to identify the widget from which the dropdown originates.
  final GlobalKey _dropdownOriginator = GlobalKey();

  @override
  Widget build(BuildContext context) {
    final button = Semantics(
      button: true,
      child: MouseRegion(
        cursor: MaterialStateMouseCursor.clickable,
        child: GestureDetector(
          key: _dropdownOriginator,
          onTap: widget.bDisabled == true ? null : _onTap,
          child: widget.buttonBuilder(context, ModalDropdownButtonState.closed),
        ),
      ),
    );

    return (widget.tooltipMessage != null)
        ? Tooltip(
            child: button,
            message: widget.tooltipMessage,
          )
        : button;
  }

  /// Called when this is tapped.
  void _onTap() {
    final RenderObject? widgetRenderObject = _dropdownOriginator.currentContext?.findRenderObject();
    final RenderBox? widgetBox = (widgetRenderObject != null) ? (widgetRenderObject as RenderBox) : null;

    if (widgetBox == null) {
      throw Exception('Failed to find widget render box for $this');
    }

    final RenderObject rootRenderObject = Navigator.of(context, rootNavigator: true).context.findRenderObject()!;

    final Size rectSize = Size(
      widgetBox.size.width + widget.openRectExtraSpace.horizontal,
      widgetBox.size.height + widget.openRectExtraSpace.vertical,
    );

    final Offset globalPos = widgetBox.localToGlobal(
      Offset(-widget.openRectExtraSpace.left, -widget.openRectExtraSpace.bottom),
      ancestor: rootRenderObject,
    );

    final Rect widgetRect = Rect.fromLTWH(globalPos.dx, globalPos.dy, rectSize.width, rectSize.height);

    DropDownListMenu.showAtRect(
      context,
      pivotRect: widgetRect,
      bStretch: widget.bStretchDropdown,
      builder: (context) => widget.menuBuilder(
        context,
        (context, bIsOnTop) => ConstrainedBox(
          constraints: BoxConstraints(
            maxWidth: widgetRect.width,
            maxHeight: widgetRect.height,
          ),
          child: widget.buttonBuilder(
            context,
            bIsOnTop ? ModalDropdownButtonState.openOnTop : ModalDropdownButtonState.openOnBottom,
          ),
        ),
      ),
    );
  }
}

/// A button that opens an automatically built dropdown list modal containing text-only options when pressed.
class ModalTextDropdownButton<ItemType> extends StatelessWidget {
  const ModalTextDropdownButton({
    Key? key,
    required this.builder,
    required this.items,
    required this.onChanged,
    this.value,
    this.makeItemName,
    this.openRectExtraSpace = EdgeInsets.zero,
    this.bStretchDropdown = false,
  }) : super(key: key);

  /// Function that builds the button based on the dropdown's state.
  final Widget Function(BuildContext context, ModalDropdownButtonState state) builder;

  /// The currently selected value.
  final ItemType? value;

  /// The list of selectable items in the order in which they should be displayed.
  final List<ItemType> items;

  /// Function called when a new value is selected.
  final void Function(ItemType newValue) onChanged;

  /// Optional function to convert items to a display string. If not provided, the items will be converted implicitly.
  final String Function(ItemType item)? makeItemName;

  /// How much space to add around the original button's bounding area when building its open version.
  final EdgeInsets openRectExtraSpace;

  /// Whether to stretch the dropdown to match the size of the button.
  final bool bStretchDropdown;

  @override
  Widget build(BuildContext context) {
    return ModalDropdownButton(
      openRectExtraSpace: openRectExtraSpace,
      bStretchDropdown: bStretchDropdown,
      buttonBuilder: builder,
      menuBuilder: (context, originTabBuilder) => DropDownListMenu(
        originTabBuilder: originTabBuilder,
        children: [
          for (ItemType item in items)
            ListMenuSimpleItem(
              title: _makeItemName(item),
              onTap: () => _onValueSelected(context, item),
            ),
        ],
      ),
    );
  }

  /// Convert an item to a display name, using the user-provided function if possible.
  String _makeItemName(ItemType item) {
    if (makeItemName != null) {
      return makeItemName!(item);
    }

    return item.toString();
  }

  /// Called when an item is selected.
  void _onValueSelected(BuildContext context, ItemType value) {
    final navigator = Navigator.of(context, rootNavigator: true);
    navigator.pop();

    onChanged(value);
  }
}

/// A dropdown button styled as a large selector containing text.
class DropdownSelector<ItemType> extends StatelessWidget {
  const DropdownSelector({
    Key? key,
    required this.value,
    required this.items,
    required this.onChanged,
    this.hint = '',
    this.makeItemName,
    this.bIsEnabled = true,
    this.borderRadius = const Radius.circular(4),
  }) : super(key: key);

  /// The currently selected value.
  final ItemType? value;

  /// The list of selectable items in the order in which they should be displayed.
  final List<ItemType> items;

  /// Function called when a new value is selected.
  final void Function(ItemType newValue) onChanged;

  /// Optional function to convert items to a display string. If not provided, the items will be converted implicitly.
  final String Function(ItemType item)? makeItemName;

  /// If false, disable the controls and grey this out.
  final bool bIsEnabled;

  /// Text to show when [value] is null.
  final String hint;

  /// The border radius of the button.
  final Radius borderRadius;

  /// The display name to show inside the dropdown button.
  String get _displayName {
    if (value == null) {
      return hint;
    }

    if (makeItemName != null) {
      return makeItemName!(value!);
    }

    return value.toString();
  }

  @override
  Widget build(BuildContext context) {
    final builder = (BuildContext context, ModalDropdownButtonState state) => DropdownSelectorStyleButton(
          state: state,
          label: _displayName,
          bIsEnabled: bIsEnabled,
          borderRadius: borderRadius,
        );

    if (bIsEnabled) {
      return ModalTextDropdownButton(
        bStretchDropdown: true,
        value: value,
        items: items,
        onChanged: onChanged,
        makeItemName: makeItemName,
        builder: builder,
      );
    } else {
      return builder(context, ModalDropdownButtonState.closed);
    }
  }
}

/// A button in the style of a modal dropdown selector.
class DropdownSelectorStyleButton extends StatelessWidget {
  const DropdownSelectorStyleButton({
    super.key,
    required this.label,
    required this.state,
    this.bIsEnabled = true,
    this.borderRadius = const Radius.circular(4),
  });

  /// The button's label text.
  final String label;

  /// The state of the dropdown. The button's style will change to reflect the state.
  final ModalDropdownButtonState state;

  /// If false, disable the controls and grey this out.
  final bool bIsEnabled;

  /// The border radius of the button.
  final Radius borderRadius;

  @override
  Widget build(BuildContext context) {
    final Color textColor = bIsEnabled ? UnrealColors.white : UnrealColors.gray56;

    return Container(
      height: 36,
      decoration: BoxDecoration(
        color: bIsEnabled ? Theme.of(context).colorScheme.background : UnrealColors.gray22,
        borderRadius: BorderRadius.vertical(
          top: borderRadius,
          bottom: state == ModalDropdownButtonState.closed ? borderRadius : Radius.zero,
        ),
      ),
      padding: EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Text(
            label,
            style: Theme.of(context).textTheme.bodyMedium!.copyWith(
                  color: textColor,
                  letterSpacing: 0.5,
                ),
          ),
          SizedBox(width: 8),
          AssetIcon(
            path: 'packages/epic_common/assets/icons/chevron_down.svg',
            size: 16,
            color: textColor,
          ),
        ],
      ),
    );
  }
}
