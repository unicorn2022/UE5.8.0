// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../../localizations.dart';
import '../../../utilities/iterable_utils.dart';
import '../../timecode/timecode_manager.dart';
import '../settings.dart';
import 'timecode_display.dart';

/// Settings menu item that displays the current timecode source and allows users to pick a new one from the sources
/// registered with the context's [TimecodeManager].
class TimecodeSettingsMenuItem extends StatelessWidget {
  const TimecodeSettingsMenuItem({super.key});

  @override
  Widget build(BuildContext context) {
    final timecodeManager = context.read<TimecodeManager>();
    return ValueListenableBuilder(
      valueListenable: timecodeManager.activeSource,
      builder: (context, timecodeSource, _) => SettingsMenuItem(
        title: EpicCommonLocalizations.of(context)!.timecodeSettingsLabel,
        iconPath: 'packages/epic_common/assets/icons/timecode.svg',
        trailing: Text(timecodeSource.getDisplayName(context)),
        onTap: () => Navigator.of(context).pushNamed(SettingsTimecodeSourcePicker.route),
      ),
    );
  }
}

/// Settings menu for picking a timecode source from the ones registered with the context's [TimecodeManager].
class SettingsTimecodeSourcePicker extends StatelessWidget {
  const SettingsTimecodeSourcePicker({super.key});

  static const String route = '/timecode';

  @override
  Widget build(BuildContext context) {
    final timecodeManager = Provider.of<TimecodeManager>(context);

    return SettingsPageScaffold(
      title: EpicCommonLocalizations.of(context)!.settingsTimecodeTitle,
      body: PreferenceBuilder(
        preference: timecodeManager.sourceName,
        builder: (context, selectedSourceName) {
          final List<String> orderedSourceNames = timecodeManager.sources.keys.toList();

          // Move the active source to the front of the list
          orderedSourceNames.remove(selectedSourceName);
          orderedSourceNames.insert(0, selectedSourceName);

          final Iterable<Widget> items = orderedSourceNames
              .map<Widget?>(
                (sourceName) =>
                    timecodeManager.sources[sourceName]?.makeSettingsEntry(sourceName == selectedSourceName),
              )
              .nonNulls
              .separate(const SettingsMenuDivider());

          return Column(
            mainAxisSize: MainAxisSize.min,
            children: items.toList(growable: false),
          );
        },
      ),
    );
  }
}

/// Base class for displaying a timecode source in the settings.
abstract class TimecodeSettingsItem extends StatelessWidget {
  const TimecodeSettingsItem(this.bIsSelected);

  /// Whether the user has selected this timecode source.
  final bool bIsSelected;

  /// Path of the icon to show trailing the widget.
  @protected
  String? get trailingIconPath => bIsSelected ? 'packages/epic_common/assets/icons/check.svg' : null;

  /// Make the timecode display widget, or null if this isn't selected.
  @protected
  Widget? buildTimecodeDisplay() => bIsSelected ? const TimecodeDisplay(bShowFraction: true) : null;
}
