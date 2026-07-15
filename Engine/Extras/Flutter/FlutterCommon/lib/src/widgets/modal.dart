// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/src/widgets/asset_icon.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../localizations.dart';
import 'epic_icon_button.dart';

/// Box shadow to show behind modals.
const List<BoxShadow> modalBoxShadow = [
  BoxShadow(
    color: Color(0x66000000),
    offset: Offset(4, 8),
    blurRadius: 8,
  )
];

/// A route that displays a generic modal dialog centered in the screen.
class GenericModalDialogRoute<T> extends PopupRoute<T> {
  GenericModalDialogRoute({
    required this.builder,
    this.bResizeToAvoidBottomInset = false,
    this.bIsBarrierDismissible = true,
  });

  /// A function that will build the widget to display within this route.
  final Widget Function(BuildContext context) builder;

  /// If true, resize the route to stay within the bottom inset of the screen (e.g. where the OS keyboard opens on
  /// mobile devices).
  final bool bResizeToAvoidBottomInset;

  /// If true, the user can dismiss this modal by tapping outside of its body.
  final bool bIsBarrierDismissible;

  @override
  Color? get barrierColor => Colors.black38;

  @override
  bool get barrierDismissible => bIsBarrierDismissible;

  @override
  String? get barrierLabel => EpicCommonLocalizations.of(navigator!.context)!.modalDismissLabel;

  @override
  Duration get transitionDuration => Duration(milliseconds: 200);

  @override
  Duration get reverseTransitionDuration => Duration(milliseconds: 100);

  @override
  Widget buildPage(BuildContext context, Animation<double> animation, Animation<double> secondaryAnimation) {
    return CustomSingleChildLayout(
      delegate: _GenericModalDialogRouteLayout<T>(
        context: context,
        bResizeToAvoidBottomInset: bResizeToAvoidBottomInset,
      ),
      child: Center(
        child: Builder(builder: builder),
      ),
    );
  }

  /// Show a dialog using a [GenericModalDialogRoute] as the route.
  /// If [bIsBarrierDismissible] is true, the user can dismiss this modal by tapping outside of its body.
  /// Returns a future that will return the result when the dialog was popped from the navigator.
  static Future<T?> showDialog<T>({
    required BuildContext context,
    required WidgetBuilder builder,
    bool bIsBarrierDismissible = true,
  }) {
    final route = GenericModalDialogRoute<T>(
      bResizeToAvoidBottomInset: true,
      builder: builder,
      bIsBarrierDismissible: bIsBarrierDismissible,
    );

    return Navigator.of(context, rootNavigator: true).push<T>(route);
  }
}

class _GenericModalDialogRouteLayout<T> extends SingleChildLayoutDelegate {
  _GenericModalDialogRouteLayout({
    required this.context,
    required this.bResizeToAvoidBottomInset,
  });

  final BuildContext context;

  /// If true, resize the route to stay within the bottom inset of the screen (e.g. where the OS keyboard opens on
  /// mobile devices).
  final bool bResizeToAvoidBottomInset;

  @override
  BoxConstraints getConstraintsForChild(BoxConstraints constraints) {
    double maxHeight = double.infinity;

    final MediaQueryData mediaQuery = MediaQuery.of(context);
    if (bResizeToAvoidBottomInset) {
      maxHeight = mediaQuery.size.height - mediaQuery.viewInsets.bottom;
    } else {
      maxHeight = mediaQuery.size.height;
    }

    return constraints.copyWith(
      minHeight: 0,
      maxHeight: maxHeight,
    );
  }

  @override
  bool shouldRelayout(covariant SingleChildLayoutDelegate oldDelegate) {
    return oldDelegate != this;
  }
}

/// A generic card wrapper for a modal dialog.
class ModalDialogCard extends StatelessWidget {
  const ModalDialogCard({
    Key? key,
    required this.child,
    this.color,
    this.shape,
  }) : super(key: key);

  /// Contents of the card.
  final Widget child;

  /// Color of the card.
  final Color? color;

  /// Shape of the card.
  final ShapeBorder? shape;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(boxShadow: modalBoxShadow),
      constraints: BoxConstraints(minWidth: 300),
      child: Card(
        shape: shape,
        color: color ?? Theme.of(context).colorScheme.surfaceTint,
        child: child,
      ),
    );
  }
}

/// Standard title formatting for a modal dialog.
class ModalDialogTitle extends StatelessWidget {
  const ModalDialogTitle({
    Key? key,
    required this.title,
    this.iconPath,
  }) : super(key: key);

  /// The text to show.
  final String title;

  /// Optional path of an icon to show next to the title.
  final String? iconPath;

  @override
  Widget build(BuildContext context) {
    return ModalDialogSection(
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 4),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            if (iconPath != null)
              Padding(
                padding: const EdgeInsets.only(right: 10),
                child: AssetIcon(path: iconPath!, size: 24),
              ),
            Text(
              this.title,
              style: Theme.of(context).textTheme.displayLarge,
              overflow: TextOverflow.fade,
            ),
          ],
        ),
      ),
    );
  }
}

/// Wrapper to apply standard padding for a section of a modal dialog.
class ModalDialogSection extends StatelessWidget {
  const ModalDialogSection({Key? key, required this.child}) : super(key: key);

  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(
        horizontal: 16,
        vertical: 8,
      ),
      child: child,
    );
  }
}

/// A generic modal containing information and an "OK" button.
class InfoModalDialog extends StatelessWidget {
  const InfoModalDialog({Key? key, required this.message}) : super(key: key);

  /// The localized message to show.
  final String message;

  /// Show an info dialog in a [context] containing a [message].
  /// Returned future completes when the dialog is popped.
  static Future show(BuildContext context, String message) {
    final route = GenericModalDialogRoute(
      bResizeToAvoidBottomInset: true,
      builder: (_) => InfoModalDialog(message: message),
    );

    return Navigator.of(context, rootNavigator: true).push(route);
  }

  @override
  Widget build(BuildContext context) {
    final localizations = EpicCommonLocalizations.of(context)!;

    return ModalDialogCard(
      child: IntrinsicWidth(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            SizedBox(height: 8),
            ModalDialogSection(
              child: Text(message),
            ),
            ModalDialogSection(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  EpicLozengeButton(
                    label: localizations.menuButtonOK,
                    width: 110,
                    onPressed: () => Navigator.of(context).pop(),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// The action the user chose in a [TextInputModalDialog].
enum TextInputModalDialogAction {
  /// The user cancelled the modal.
  cancel,

  /// The user entered a new value and applied it.
  apply,

  /// The user pressed the reset button.
  reset,
}

/// The result returned when a [TextInputModalDialog] is popped.
class TextInputModalDialogResult<T> {
  const TextInputModalDialogResult({required this.action, this.value = null});

  /// The result of the user's interaction with the dialog.
  final TextInputModalDialogAction action;

  /// The text that the user entered. Will be null unless result is [TextInputModalDialogAction.apply], in which case
  /// it contains the value parsed from the user's text.
  final T? value;
}

/// A generic modal that lets the user input a value using the keyboard.
/// When popped, this pushes a [TextInputModalDialogResult] with the result of the modal interaction.
class TextInputModalDialog<T> extends StatefulWidget {
  const TextInputModalDialog({
    Key? key,
    required this.title,
    required this.parseValue,
    this.handleResult,
    this.initialText,
    this.hintText = '',
    this.keyboardType,
    this.inputFormatters,
    this.bShowResetButton = false,
  }) : super(key: key);

  /// The localized title of the dialog.
  final String title;

  /// A function that takes the user's text value and converts it to the desired result type.
  final T? Function(String) parseValue;

  /// An optional function that takes the result and returns a future which will complete once the modal should close.
  final Future Function(TextInputModalDialogResult<T>)? handleResult;

  /// The initial text to show in the input field.
  final String? initialText;

  /// Hint text to display in the text box.
  final String hintText;

  /// Input type for the text box.
  final TextInputType? keyboardType;

  /// List of text input formatters to apply to the text.
  final List<TextInputFormatter>? inputFormatters;

  /// If true, show a button allowing the user to reset the value.
  final bool bShowResetButton;

  @override
  State<StatefulWidget> createState() => _TextInputModalDialogState<T>();
}

class _TextInputModalDialogState<T> extends State<TextInputModalDialog<T>> {
  /// Text controller for the input field displayed to the user.
  final _textController = TextEditingController();

  /// If true, this has completed and is waiting for the result to be handled.
  bool bIsResultBeingHandled = false;

  @override
  void initState() {
    super.initState();

    if (widget.initialText != null) {
      _textController.text = widget.initialText!;
      _textController.selection = TextSelection(baseOffset: 0, extentOffset: _textController.text.length);
    }
  }

  @override
  Widget build(BuildContext context) {
    final localizations = EpicCommonLocalizations.of(context)!;

    final List<TextInputFormatter> formatters = [];
    if (widget.inputFormatters != null) {
      formatters.addAll(widget.inputFormatters!);
    }

    formatters.add(TextInputFormatter.withFunction(_takeNewIfParsed));

    return Form(
      autovalidateMode: AutovalidateMode.onUserInteraction,
      child: ModalDialogCard(
        child: IntrinsicHeight(
          child: IntrinsicWidth(
            child: Column(
              children: [
                ModalDialogTitle(title: widget.title),
                if (bIsResultBeingHandled)
                  ModalDialogSection(
                    child: Padding(
                      padding: EdgeInsets.only(bottom: 16),
                      child: SizedBox.square(
                        dimension: 100,
                        child: CircularProgressIndicator(
                          strokeWidth: 6,
                        ),
                      ),
                    ),
                  ),
                if (!bIsResultBeingHandled)
                  ModalDialogSection(
                    child: Row(
                      children: [
                        Expanded(
                          child: SizedBox(
                            height: 36,
                            child: TextField(
                              autofocus: true,
                              maxLines: 1,
                              cursorWidth: 1,
                              controller: _textController,
                              keyboardAppearance: Brightness.dark,
                              style: Theme.of(context).textTheme.bodyMedium,
                              decoration: InputDecoration(hintText: widget.hintText),
                              keyboardType: widget.keyboardType,
                              inputFormatters: formatters,
                              onEditingComplete: _complete,
                            ),
                          ),
                        ),
                        if (widget.bShowResetButton)
                          Padding(
                            padding: const EdgeInsets.only(left: 8),
                            child: _ResetValueButton(onPressed: _reset),
                          ),
                      ],
                    ),
                  ),
                if (!bIsResultBeingHandled)
                  ModalDialogSection(
                    child: Padding(
                      padding: EdgeInsets.symmetric(horizontal: 8),
                      child: Row(
                        mainAxisAlignment: MainAxisAlignment.spaceAround,
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Flexible(
                            flex: 4,
                            fit: FlexFit.tight,
                            child: EpicLozengeButton(
                              label: localizations.menuButtonCancel,
                              color: Colors.transparent,
                              onPressed: _cancel,
                            ),
                          ),
                          Spacer(flex: 1),
                          Flexible(
                            flex: 4,
                            fit: FlexFit.tight,
                            child: EpicLozengeButton(
                              label: localizations.menuButtonOK,
                              onPressed: _complete,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  /// Take the new text editing value only if it passes the parsing function.
  TextEditingValue _takeNewIfParsed(TextEditingValue oldValue, TextEditingValue newValue) {
    if (widget.parseValue(newValue.text) == null) {
      return oldValue;
    }

    return newValue;
  }

  /// Complete the interaction and pop the modal, returning the entered value.
  void _complete() async {
    _handleResult(TextInputModalDialogResult<T>(
      action: TextInputModalDialogAction.apply,
      value: widget.parseValue(_textController.value.text),
    ));
  }

  /// Cancel the interaction and pop the modal, returning a cancelled result.
  void _cancel() {
    _handleResult(TextInputModalDialogResult<T>(action: TextInputModalDialogAction.cancel));
  }

  /// Cancel the interaction and pop the modal, returning a reset result.
  void _reset() {
    _handleResult(TextInputModalDialogResult<T>(action: TextInputModalDialogAction.reset));
  }

  /// Handle the result of the dialog, either popping or waiting for the provided result handler to complete.
  void _handleResult(TextInputModalDialogResult<T> result) async {
    if (bIsResultBeingHandled) {
      return;
    }

    if (widget.handleResult != null) {
      setState(() {
        bIsResultBeingHandled = true;
      });

      await widget.handleResult!(result);
    }

    Navigator.of(context).pop(result);
  }
}

/// A generic modal that lets the user input a [double] using the keyboard.
class DoubleTextInputModalDialog extends StatelessWidget {
  const DoubleTextInputModalDialog({
    Key? key,
    required this.title,
    this.handleResult,
    this.initialValue,
    this.keyboardType,
    this.inputFormatters,
    this.bShowResetButton = false,
  }) : super(key: key);

  /// The localized title of the dialog.
  final String title;

  /// An optional function that takes the result and returns a future which will complete once the modal should close.
  final Future Function(TextInputModalDialogResult<double>)? handleResult;

  /// The initial value to show in the input field.
  final double? initialValue;

  /// Input type for the text box.
  final TextInputType? keyboardType;

  /// List of text input formatters to apply to the text.
  final List<TextInputFormatter>? inputFormatters;

  /// If true, show a button allowing the user to reset the value.
  final bool bShowResetButton;

  @override
  Widget build(BuildContext context) {
    return TextInputModalDialog<double>(
      title: title,
      handleResult: handleResult,
      initialText: initialValue?.toStringAsFixed(6),
      hintText: '0',
      parseValue: _parseDouble,
      keyboardType: keyboardType ?? TextInputType.numberWithOptions(decimal: true),
      bShowResetButton: bShowResetButton,
      // Additional formatter ensures we always accept numeric values, period & a negative sign (-)
      inputFormatters: inputFormatters ?? [FilteringTextInputFormatter(RegExp('[0-9.-]'), allow: true)],
    );
  }

  /// Try to convert the text value to a double.
  double? _parseDouble(String text) {
    if (text.isNotEmpty) {
      if (text == "-" || text == "-." || text == ".") {
        // Returning new value if text field contains only "." or "-." or ".".
        return double.tryParse('${text}0');
      } else {
        // if the above condition falls through, it means we can only possibly expect a numeric value hence we parse
        // without further manipulation/
        return double.tryParse(text);
      }
    } else {
      return 0;
    }
  }
}

/// A generic modal that lets the user input an [int] using the keyboard.
class IntegerTextInputModalDialog extends StatelessWidget {
  const IntegerTextInputModalDialog({
    Key? key,
    required this.title,
    this.handleResult,
    this.initialValue,
    this.keyboardType,
    this.inputFormatters,
    this.bShowResetButton = false,
  }) : super(key: key);

  /// The localized title of the dialog.
  final String title;

  /// An optional function that takes the result and returns a future which will complete once the modal should close.
  final Future Function(TextInputModalDialogResult<int>)? handleResult;

  /// The initial value to show in the input field.
  final int? initialValue;

  /// Input type for the text box.
  final TextInputType? keyboardType;

  /// List of text input formatters to apply to the text.
  final List<TextInputFormatter>? inputFormatters;

  /// If true, show a button allowing the user to reset the value.
  final bool bShowResetButton;

  @override
  Widget build(BuildContext context) {
    return TextInputModalDialog<int>(
      title: title,
      handleResult: handleResult,
      initialText: initialValue?.toString(),
      hintText: '0',
      parseValue: _parseInt,
      keyboardType: keyboardType ?? TextInputType.numberWithOptions(decimal: false),
      bShowResetButton: bShowResetButton,
      // Additional formatter ensures we always accept numeric values, period & a negative sign (-)
      inputFormatters: inputFormatters ?? [FilteringTextInputFormatter(RegExp('[0-9-]'), allow: true)],
    );
  }

  /// Try to convert the text value to an integer.
  int? _parseInt(String text) {
    if (text.isNotEmpty) {
      if (text == '-') {
        // Returning new value if text field contains only a negative sign
        return int.tryParse('${text}0');
      } else {
        // if the above condition falls through, it means we can only possibly expect a numeric value hence we parse
        // without further manipulation
        return int.tryParse(text);
      }
    } else {
      return 0;
    }
  }
}

/// A generic modal that lets the user input a [String] using the keyboard.
class StringTextInputModalDialog extends StatelessWidget {
  const StringTextInputModalDialog({
    Key? key,
    required this.title,
    this.handleResult,
    this.initialValue,
    this.hintText = '',
    this.keyboardType,
    this.inputFormatters,
    this.bShowResetButton = false,
  }) : super(key: key);

  /// The localized title of the dialog.
  final String title;

  /// An optional function that takes the result and returns a future which will complete once the modal should close.
  final Future Function(TextInputModalDialogResult<String>)? handleResult;

  /// The initial value to show in the input field.
  final String? initialValue;

  /// Hint text to display in the text box.
  final String hintText;

  /// Input type for the text box.
  final TextInputType? keyboardType;

  /// List of text input formatters to apply to the text.
  final List<TextInputFormatter>? inputFormatters;

  /// If true, show a button allowing the user to reset the value.
  final bool bShowResetButton;

  @override
  Widget build(BuildContext context) {
    return TextInputModalDialog<String>(
      title: title,
      handleResult: handleResult,
      initialText: initialValue,
      hintText: hintText,
      parseValue: (value) => value,
      keyboardType: keyboardType ?? TextInputType.text,
      bShowResetButton: bShowResetButton,
      inputFormatters: inputFormatters,
    );
  }
}

/// Button used to reset the value in a modal.
class _ResetValueButton extends StatelessWidget {
  const _ResetValueButton({this.onPressed, Key? key}) : super(key: key);

  /// Callback for when the button is pressed.
  final Function()? onPressed;

  @override
  Widget build(BuildContext context) {
    return EpicIconButton(
      onPressed: onPressed,
      iconPath: 'packages/epic_common/assets/icons/reset.svg',
      tooltipMessage: EpicCommonLocalizations.of(context)!.resetButtonTooltip,
      iconSize: 24,
      buttonSize: const Size(32, 32),
    );
  }
}
