// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';

import 'wip_log_list.dart';
import 'wip_log_view.dart';

/// Temporary dialog containing the app's logs.
class WipLogDialog extends StatefulWidget {
  const WipLogDialog({Key? key}) : super(key: key);

  static void show(BuildContext context) {
    final route = GenericModalDialogRoute(
      builder: (_) => WipLogDialog(),
    );

    Navigator.of(context, rootNavigator: true).push(route);
  }

  @override
  State<StatefulWidget> createState() => _WipLogDialogState();
}

class _WipLogDialogState extends State<WipLogDialog> with RouteAware {
  /// Key corresponding to the inner navigator of the log dialog.
  final _innerNavigatorKey = GlobalKey<NavigatorState>();

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: ModalDialogCard(
        color: Theme.of(context).colorScheme.surface,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(16),
        ),
        child: ConstrainedBox(
          constraints: BoxConstraints(
            maxWidth: 600,
            minHeight: 250,
          ),
          child: IntrinsicHeight(
            child: Navigator(
              key: _innerNavigatorKey,
              onGenerateRoute: (final RouteSettings settings) {
                late final Widget page;

                switch (settings.name) {
                  case WipLogList.route:
                    page = const WipLogList();
                    break;

                  case WipLogView.route:
                    page = const WipLogView();
                    break;

                  default:
                    throw Exception('No log route named ${settings.name}');
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
