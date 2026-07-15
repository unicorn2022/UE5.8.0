// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/src/widgets/framework.dart';
import 'package:provider/provider.dart';

import '../../../localizations.dart';
import '../widgets/settings.dart';
import '../widgets/timecode/timecode_settings.dart';
import 'timecode.dart';
import 'timecode_manager.dart';
import 'timecode_source.dart';
import 'timecode_synchronizer.dart';

/// Provides timecodes synchronized with the system clock.
class SystemClockTimecodeSource extends TimecodeSource {
  static const String staticInternalName = 'systemClock';
  static const String staticIconPath = 'packages/epic_common/assets/icons/timecode_system.svg';

  @override
  String getDisplayName(BuildContext context) => EpicCommonLocalizations.of(context)!.timecodeSourceSystem;

  @override
  String get iconPath => staticIconPath;

  @override
  String get internalName => staticInternalName;

  @override
  Widget makeSettingsEntry(bool bIsSelected) => _SystemTimecodeSettingsItem(bIsSelected);

  @override
  TimecodeSynchronizer makeSynchronizer(BuildContext) => _SystemClockTimecodeSynchronizer();
}

class _SystemClockTimecodeSynchronizer implements TimecodeSynchronizer {
  @override
  Timecode get timecode => Timecode.fromDateTime(DateTime.now());

  @override
  void dispose() {}
}

class _SystemTimecodeSettingsItem extends TimecodeSettingsItem {
  const _SystemTimecodeSettingsItem(super.bIsSelected);

  @override
  Widget build(BuildContext context) {
    final timecodeManager = Provider.of<TimecodeManager>(context);

    return SettingsMenuItem(
      title: EpicCommonLocalizations.of(context)!.timecodeSourceSystem,
      onTap: () => timecodeManager.sourceName.setValue(SystemClockTimecodeSource.staticInternalName),
      iconPath: SystemClockTimecodeSource.staticIconPath,
      trailingIconPath: trailingIconPath,
      trailing: buildTimecodeDisplay(),
      bAlwaysPadForTrailingIcon: true,
    );
  }
}
