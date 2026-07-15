// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/localizations.dart';
import 'package:epic_common/utilities/version.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../../../models/settings/vcam_settings.dart';
import '../../../../util/nav_key.dart';
import '../../../models/net/connection_enum.dart';
import '../../../models/net/engine_connection_manager.dart';
import '../../../uri.dart';
import 'eula/eula_screen.dart';
import 'settings_developer.dart';
import 'settings_log_list.dart';
import 'settings_third_party_notice_list.dart';

/// Main screen of the [SettingsDialog].
class SettingsMain extends StatelessWidget {
  const SettingsMain({Key? key}) : super(key: key);

  static const String route = '/';

  @override
  Widget build(BuildContext context) {
    final localizations = AppLocalizations.of(context)!;
    final settings = Provider.of<VCamSettings>(context);
    final connectionManager = Provider.of<EngineConnectionManager>(context);

    return SettingsPageScaffold(
      title: localizations.settingsDialogTitle,
      body: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const TimecodeSettingsMenuItem(),
          const SettingsMenuDivider(),
          SettingsMenuItem(
            title: localizations.settingsDialogApplicationLogLabel,
            iconPath: 'packages/epic_common/assets/icons/log.svg',
            onTap: () => Navigator.of(context).pushNamed(VcamSettingLogList.route),
          ),
          const SettingsMenuDivider(),
          PreferenceBuilder(
            preference: settings.bShowInfoBar,
            builder: (context, value) {
              return SettingsMenuItem(
                title: value ? localizations.settingsHideInfoBarLabel : localizations.settingsShowInfoBarLabel,
                iconPath: 'assets/icons/bar_chart.svg',
                trailingIconPath: null,
                onTap: () => settings.bShowInfoBar.setValue(!value),
              );
            },
          ),
          PreferenceBuilder(
            preference: settings.bDeveloperModeEnabled,
            builder: (context, value) {
              return SettingsMenuItem(
                title: localizations.settingsDeveloperTitle,
                iconPath: 'assets/icons/developer.svg',
                trailing: Text(value ? localizations.settingsGenericEnabled : localizations.settingsGenericDisabled),
                onTap: () => Navigator.of(context).pushNamed(SettingsDeveloper.route),
              );
            },
          ),
          const SettingsMenuDivider(),
          SettingsMenuItem(
            title: localizations.settingsHelpButtonLabel,
            iconPath: 'packages/epic_common/assets/icons/help.svg',
            trailingIconPath: null,
            onTap: () => launchUrl(Uri.parse(helpUri)),
          ),
          SettingsMenuItem(
            title: localizations.settingsAboutButtonLabel,
            iconPath: 'packages/epic_common/assets/icons/info.svg',
            onTap: () => rootNavigatorKey.currentState?.pushNamed(EulaScreen.route),
          ),
          SettingsMenuItem(
            title: localizations.settingsPrivacyPolicyLabel,
            iconPath: 'packages/epic_common/assets/icons/info.svg',
            onTap: () => launchUrl(Uri.parse(privacyPolicyUri)),
          ),
          SettingsMenuItem(
            title: EpicCommonLocalizations.of(context)!.settingsThirdPartyNoticesTitle,
            iconPath: 'packages/epic_common/assets/icons/info.svg',
            onTap: () => Navigator.of(context).pushNamed(VcamSettingsThirdPartyNoticeList.route),
          ),
          const SettingsMenuDivider(),
          Container(
            padding: EdgeInsets.symmetric(horizontal: 8),
            height: 54,
            child: Row(
              children: [
                ValueListenableBuilder(
                  valueListenable: connectionManager.connectionState,
                  builder: (context, connectionState, _) => connectionState == EngineConnectionState.connected
                      ? DisconnectButton(onTap: connectionManager.disconnect)
                      : const SizedBox(),
                ),
                const Spacer(),
                FutureBuilder<String>(
                  future: getFriendlyPackageVersion(),
                  builder: (context, snapshot) => Text(
                    snapshot.data ?? '',
                    style: Theme.of(context).textTheme.labelMedium,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
