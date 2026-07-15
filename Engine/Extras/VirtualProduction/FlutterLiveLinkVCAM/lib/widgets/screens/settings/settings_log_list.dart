// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';

import 'settings_log_view.dart';

class VcamSettingLogList extends StatelessWidget {
  const VcamSettingLogList({super.key});

  static const String route = '/application_log';

  @override
  Widget build(BuildContext context) {
    return LogListSettingsPage(
      AppLocalizations.of(context)!.settingsDialogApplicationLogLabel,
      VcamSettingsLogView.route,
    );
  }
}
