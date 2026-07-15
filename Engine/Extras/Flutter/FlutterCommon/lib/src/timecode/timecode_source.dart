// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/widgets.dart';

import 'timecode_synchronizer.dart';

/// Provides all the data relevant to a selectable source for a timecode.
abstract class TimecodeSource {
  /// The internal name for the source.
  String get internalName;

  /// The path to the icon for the source.
  String get iconPath;

  /// Optional function that allows the timecode source to extend the settings menu.
  /// Takes a [route] path and returns the contents of the page to display at that route, or null if the route isn't
  /// handled for this source.
  /// The handled routes are assumed to be unique among timecode pages, and should start with "/timecode".
  Widget? Function(String route)? get getSettingsPage => null;

  /// Get the user-facing name of the source in the given localization [context].
  String getDisplayName(BuildContext context);

  /// Make an object that produces a timecode synchronized with the relevant source in the given build [context].
  TimecodeSynchronizer makeSynchronizer(BuildContext context);

  /// Make a settings menu entry for this timecode source, adjusting the styling based on whether the relevant timecode
  /// source [bIsSelected] in the user preferences.
  Widget makeSettingsEntry(bool bIsSelected);
}
