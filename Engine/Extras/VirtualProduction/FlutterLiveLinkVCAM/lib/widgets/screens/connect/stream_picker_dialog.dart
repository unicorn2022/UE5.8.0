// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/localizations.dart';
import 'package:epic_common/theme.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

/// Show the form for picking which WebRTC streamer to subscribe to from the list of [streamers].
/// Future completes with the streamer ID to use, or null if the user cancelled.
Future<String?> showStreamPicker(BuildContext context, List<String> streamers) {
  final route = GenericModalDialogRoute<String>(
    bResizeToAvoidBottomInset: true,
    builder: (_) => _StreamPicker(streamers),
  );

  return Navigator.of(context, rootNavigator: true).push(route);
}

/// Picker dialog for which WebRTC streamer to subscribe to.
class _StreamPicker extends StatefulWidget {
  const _StreamPicker(this.streamers);

  /// The list of streamer options by ID.
  final List<String> streamers;

  @override
  State<_StreamPicker> createState() => _StreamPickerState();
}

class _StreamPickerState extends State<_StreamPicker> {
  /// The currently selected streamer.
  String? _selectedStreamer;

  @override
  Widget build(BuildContext context) {
    return ModalDialogCard(
      child: Container(
        width: MediaQuery.of(context).size.width * .4,
        padding: EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ModalDialogTitle(title: AppLocalizations.of(context)!.streamPickerModalTitle),
            Expanded(
              child: EpicScrollView(
                bFadeEdges: true,
                child: ModalDialogSection(
                  child: Column(
                    children: [
                      for (final streamer in widget.streamers)
                        _StreamerListEntry(
                          streamer: streamer,
                          bSelected: _selectedStreamer == streamer,
                          onTap: () => setState(() {
                            _selectedStreamer = streamer;
                          }),
                        ),
                    ],
                  ),
                ),
              ),
            ),
            ModalDialogSection(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  EpicLozengeButton(
                    onPressed: () => Navigator.pop(context),
                    label: EpicCommonLocalizations.of(context)!.menuButtonCancel,
                    color: (_selectedStreamer != null) ? Colors.transparent : Theme.of(context).colorScheme.secondary,
                  ),
                  if (_selectedStreamer != null)
                    Padding(
                      padding: EdgeInsets.only(left: 10),
                      child: EpicLozengeButton(
                        onPressed: () => Navigator.of(context).pop(_selectedStreamer),
                        label: EpicCommonLocalizations.of(context)!.menuButtonConnect,
                      ),
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

/// An entry in the list of streamers.
class _StreamerListEntry extends StatelessWidget {
  const _StreamerListEntry({
    Key? key,
    required this.streamer,
    required this.bSelected,
    required this.onTap,
  }) : super(key: key);

  /// The streamer's ID.
  final String streamer;

  /// Whether this streamer is currently selected.
  final bool bSelected;

  /// Callback called when the user taps on this option.
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      child: Container(
        decoration: BoxDecoration(
          border: Border.all(width: 3, color: bSelected ? Theme.of(context).colorScheme.primary : UnrealColors.gray42),
          borderRadius: BorderRadius.circular(8),
          color: bSelected ? Theme.of(context).colorScheme.primary : null,
        ),
        padding: EdgeInsets.all(10),
        margin: EdgeInsets.symmetric(horizontal: 30, vertical: 5),
        child: Row(
          children: [
            Text(
              streamer,
              style: Theme.of(context)
                  .textTheme
                  .titleLarge!
                  .copyWith(color: bSelected ? Theme.of(context).colorScheme.onPrimary : UnrealColors.gray56),
            ),
          ],
        ),
      ),
    );
  }
}
