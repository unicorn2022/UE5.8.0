// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/localizations.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import '../../../../models/settings/vcam_settings.dart';

/// Settings sub-menu for developer debug settings.
class SettingsDeveloper extends StatelessWidget {
  const SettingsDeveloper({Key? key}) : super(key: key);

  static const String route = '/developer';

  @override
  Widget build(BuildContext context) {
    final localizations = AppLocalizations.of(context)!;
    final settings = Provider.of<VCamSettings>(context);

    return SettingsPageScaffold(
      title: localizations.settingsDeveloperTitle,
      body: PreferenceBuilder(
        preference: settings.bDeveloperModeEnabled,
        builder: (context, bDeveloperModeEnabled) => Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            SettingsMenuItem(
              title: bDeveloperModeEnabled
                  ? localizations.settingsDeveloperModeDisableLabel
                  : localizations.settingsDeveloperModeEnableLabel,
              iconPath: 'assets/icons/developer.svg',
              trailingIconPath: null,
              onTap: () => _toggleDeveloperMode(context),
            ),
            const SettingsMenuDivider(),
            if (bDeveloperModeEnabled) ...[
              PreferenceBuilder(
                preference: settings.bShowPixelStreamingStats,
                builder: (context, value) {
                  return SettingsMenuItem(
                    title:
                        value ? localizations.hidePixelStreamingStatsLabel : localizations.showPixelStreamingStatsLabel,
                    iconPath: 'assets/icons/stream_stats.svg',
                    trailingIconPath: null,
                    onTap: () => settings.bShowPixelStreamingStats.setValue(!value),
                  );
                },
              ),
              PreferenceBuilder(
                preference: settings.maxArUpdatesPerSecond,
                builder: (context, maxArUpdatesPerSecond) {
                  return SettingsMenuItem(
                    title: localizations.settingsDeveloperMaxArUpdateRateLabel,
                    iconPath: 'assets/icons/update_rate.svg',
                    trailing: Text(maxArUpdatesPerSecond.toString()),
                    onTap: () => _showIntegerPreferenceModal(
                      context: context,
                      preference: settings.maxArUpdatesPerSecond,
                      title: localizations.settingsDeveloperMaxArUpdateRateLabel,
                    ),
                  );
                },
              ),
              PreferenceBuilder(
                preference: settings.arUpdateSkipChance,
                builder: (context, maxArUpdatesPerSecond) {
                  return SettingsMenuItem(
                    title: localizations.settingsDeveloperSkipArUpdateChance,
                    leading: AssetIcon(path: 'assets/icons/skip_chance.svg'),
                    trailing: Text(maxArUpdatesPerSecond.toString()),
                    onTap: () => _showIntegerPreferenceModal(
                      context: context,
                      preference: settings.arUpdateSkipChance,
                      title: localizations.settingsDeveloperSkipArUpdateChance,
                    ),
                  );
                },
              ),
            ],
          ],
        ),
      ),
    );
  }

  /// Enable/disable developer mode.
  void _toggleDeveloperMode(BuildContext context) async {
    final settings = Provider.of<VCamSettings>(context, listen: false);

    if (settings.bDeveloperModeEnabled.getValue()) {
      settings.bDeveloperModeEnabled.setValue(false);
      return;
    }

    final bool bShouldEnable = await GenericModalDialogRoute.showDialog(
      context: context,
      builder: (context) => const _EnableDeveloperModeModal(),
    );

    if (bShouldEnable) {
      settings.bDeveloperModeEnabled.setValue(true);
    }
  }

  /// Show a modal to set or reset an integer [preference] with the given [title].
  void _showIntegerPreferenceModal({
    required BuildContext context,
    required Preference<int> preference,
    required String title,
  }) async {
    final result = await GenericModalDialogRoute.showDialog<TextInputModalDialogResult<int>>(
      context: context,
      builder: (context) => IntegerTextInputModalDialog(
        title: title,
        initialValue: preference.getValue(),
        bShowResetButton: true,
      ),
    );

    if (result?.action == TextInputModalDialogAction.reset) {
      preference.setValue(preference.defaultValue);
      return;
    }

    if (result?.action != TextInputModalDialogAction.apply || result?.value == null) {
      return;
    }

    preference.setValue(result!.value!);
  }
}

/// Modal shown when enabling developer mode.
class _EnableDeveloperModeModal extends StatelessWidget {
  const _EnableDeveloperModeModal();

  @override
  Widget build(BuildContext context) {
    return ModalDialogCard(
      child: SizedBox(
        width: 400,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ModalDialogTitle(
              title: AppLocalizations.of(context)!.settingsDeveloperModeEnableDialogTitle,
              iconPath: 'packages/epic_common/assets/icons/alert_triangle_large.svg',
            ),
            ModalDialogSection(
              child: Text(AppLocalizations.of(context)!.settingsDeveloperModeEnableDialogBody),
            ),
            ModalDialogSection(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  EpicLozengeButton(
                    onPressed: () => Navigator.of(context).pop(false),
                    label: EpicCommonLocalizations.of(context)!.menuButtonCancel,
                    color: Colors.transparent,
                  ),
                  SizedBox(width: 16),
                  EpicLozengeButton(
                    onPressed: () => Navigator.of(context).pop(true),
                    label: EpicCommonLocalizations.of(context)!.menuButtonProceed,
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
