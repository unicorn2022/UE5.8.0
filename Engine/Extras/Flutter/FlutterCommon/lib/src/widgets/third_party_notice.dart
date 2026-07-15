// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/localizations.dart';
import 'package:epic_common/src/third_party_notice/third_party_notice.dart';
import 'package:epic_common/src/widgets/epic_list_view.dart';
import 'package:epic_common/src/widgets/settings.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

/// Settings page that displays a list of all third-party notices.
/// Requires a ThirdPartyNoticeManifest in the context in order to retrieve notices.
class ThirdPartyNoticeListSettingsPage extends StatelessWidget {
  const ThirdPartyNoticeListSettingsPage({super.key, required this.viewRoute});

  /// Route for the individual third-party notice view.
  final String viewRoute;

  Widget build(BuildContext context) {
    return SettingsPageScaffold(
      title: EpicCommonLocalizations.of(context)!.settingsThirdPartyNoticesTitle,
      bWrapBodyInScrollView: false,
      body: SizedBox(
        height: 240,
        child: MediaQuery.removePadding(
          removeTop: true,
          removeBottom: true,
          context: context,
          child: FutureBuilder(
            future: Provider.of<ThirdPartyNoticeManifest>(context).listAll(),
            builder: (context, snapshot) {
              if (!snapshot.hasData) {
                return const Center(
                  child: SizedBox.square(
                    dimension: 120,
                    child: CircularProgressIndicator(),
                  ),
                );
              }

              final List<ThirdPartyNotice> notices = snapshot.requireData;

              return EpicListView(
                itemCount: snapshot.requireData.length,
                itemBuilder: (context, index) {
                  final ThirdPartyNotice notice = notices[index];

                  return SettingsMenuItem(
                    title: notice.softwareName,
                    titleOverflow: null,
                    titleSoftWrap: true,
                    leading: const Icon(Icons.description_outlined),
                    onTap: () => Navigator.of(context).pushNamed(
                      viewRoute,
                      arguments: ThirdPartyNoticeViewSettingsPageArguments(notice: notice),
                    ),
                  );
                },
              );
            },
          ),
        ),
      ),
    );
  }
}

/// Arguments passed to the route when a [ThirdPartyNoticeViewSettingsPage] is pushed to the navigation stack.
class ThirdPartyNoticeViewSettingsPageArguments {
  const ThirdPartyNoticeViewSettingsPageArguments({required this.notice});

  /// The notice to display.
  final ThirdPartyNotice notice;
}

/// Settings page that displays an individual log.
class ThirdPartyNoticeViewSettingsPage extends StatelessWidget {
  const ThirdPartyNoticeViewSettingsPage({super.key});

  /// If a license file contains a line longer than this, it will be displayed with soft wrap enabled.
  static const int _softWrapLineThreshold = 100;

  @override
  Widget build(BuildContext context) {
    final arguments = ModalRoute.of(context)?.settings.arguments as ThirdPartyNoticeViewSettingsPageArguments;
    final notice = arguments.notice;

    return SettingsPageScaffold(
      title: EpicCommonLocalizations.of(context)!.settingsThirdPartyNoticeTitle,
      body: Padding(
        padding: const EdgeInsets.symmetric(
          horizontal: 8,
          vertical: 6,
        ),
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 750),
          child: FutureBuilder(
            future: notice.loadLicenseText(),
            builder: (context, licenseText) {
              if (!licenseText.hasData) {
                return const Center(
                  child: SizedBox.square(
                    dimension: 120,
                    child: CircularProgressIndicator(),
                  ),
                );
              }

              // If any lines exceed our length limit, turn on soft wrap to ensure the license is readable. Otherwise,
              // assume a monospace, fixed-width display.
              final licenseString = licenseText.requireData;
              bool bUseSoftWrap = false;
              for (final line in licenseString.split('\r\n')) {
                if (line.length > _softWrapLineThreshold) {
                  bUseSoftWrap = true;
                  break;
                }
              }

              final textWidget = Text(
                '${notice.softwareName} ${notice.softwareVersion}\n\n'
                '---\n\n'
                '${licenseText.requireData}',
                style: const TextStyle(
                  fontFamily: 'Droid Sans Mono',
                  fontSize: 12,
                  height: 1.2,
                  fontFeatures: [
                    FontFeature.tabularFigures(),
                  ],
                ),
                softWrap: bUseSoftWrap,
              );

              if (bUseSoftWrap) {
                return textWidget;
              }

              // Wrap the widget with a fitted box so the text always fits on-screen horizontally
              return FittedBox(
                fit: BoxFit.fitWidth,
                child: textWidget,
              );
            },
          ),
        ),
      ),
    );
  }
}
