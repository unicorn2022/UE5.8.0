// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/timecode.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../../../util/nav_key.dart';
import 'settings_developer.dart';
import 'settings_log_list.dart';
import 'settings_log_view.dart';
import 'settings_main.dart';
import 'settings_third_party_notice_list.dart';
import 'settings_third_party_notice_view.dart';

/// Dialog containing the app's settings menu.
class SettingsDialog extends StatefulWidget {
  const SettingsDialog({Key? key}) : super(key: key);

  /// show method for out settings modal.
  static void show() {
    final route = GenericModalDialogRoute(
      builder: (_) => SettingsDialog(),
    );

    Navigator.of(rootNavigatorKey.currentContext!, rootNavigator: true).push(route);
  }

  @override
  State<StatefulWidget> createState() => _SettingsDialogState();
}

class _SettingsDialogState extends State<SettingsDialog> with RouteAware {
  /// Key corresponding to the inner navigator of the settings menu.
  final _innerNavigatorKey = GlobalKey<NavigatorState>();

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: ModalDialogCard(
        color: Theme.of(context).colorScheme.surface,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        child: ConstrainedBox(
          constraints: BoxConstraints(maxWidth: 600, minHeight: 0),
          child: IntrinsicHeight(
            child: Navigator(
              key: _innerNavigatorKey,
              onGenerateRoute: (final RouteSettings settings) {
                final String? route = settings.name;

                late final Widget page;
                switch (route) {
                  case SettingsDeveloper.route:
                    page = const SettingsDeveloper();

                  case SettingsMain.route:
                    page = const SettingsMain();

                  case VcamSettingLogList.route:
                    page = const VcamSettingLogList();

                  case VcamSettingsLogView.route:
                    page = const VcamSettingsLogView();

                  case VcamSettingsThirdPartyNoticeList.route:
                    page = const VcamSettingsThirdPartyNoticeList();

                  case VcamSettingsThirdPartyNoticeView.route:
                    page = const VcamSettingsThirdPartyNoticeView();

                  default:
                    if (route != null) {
                      final Widget? timecodePage = context.read<TimecodeManager>().getTimecodeSettingsPage(route);
                      if (timecodePage != null) {
                        page = timecodePage;
                        break;
                      }
                    }

                    throw Exception('No settings route named ${route}');
                }

                return PageRouteBuilder(
                  settings: settings,
                  transitionDuration: Duration.zero,
                  reverseTransitionDuration: Duration.zero,
                  pageBuilder: (_, __, ___) => page,
                );
              },
            ),
          ),
        ),
      ),
    );
  }
}
