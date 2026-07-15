// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

import '../screens/settings/settings_dialog.dart';

/// Button that opens the settings menu.
class SettingsButton extends StatelessWidget {
  const SettingsButton({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return EpicIconButton(
      tooltipMessage: AppLocalizations.of(context)!.settingsButtonTooltip,
      iconPath: 'packages/epic_common/assets/icons/settings.svg',
      onPressed: SettingsDialog.show,
    );
  }
}
