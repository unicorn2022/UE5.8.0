// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:convert';

import 'package:epic_common/utilities/version.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:flutter_widget_from_html_core/flutter_widget_from_html_core.dart';
import 'package:html/dom.dart' as dom;
import 'package:url_launcher/url_launcher.dart';

/// Screen containing the end user license agreement.
class EulaScreen extends StatefulWidget {
  const EulaScreen({super.key});

  static const String route = '/eula';

  @override
  State<EulaScreen> createState() => _EulaScreenState();
}

class _EulaScreenState extends State<EulaScreen> {
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text('License')),
      bottomNavigationBar: InkWell(
        onTap: () => Navigator.maybePop(context),
        child: Container(
          color: Theme.of(context).colorScheme.surface,
          height: 50,
          child: Center(child: Text('Close')),
        ),
      ),
      body: FutureBuilder(
        future: DefaultAssetBundle.of(context).load('assets/eula.html'),
        builder: (context, eulaDataSnapshot) {
          if (!eulaDataSnapshot.hasData) {
            return const SizedBox();
          }

          final String text = utf8.decode(eulaDataSnapshot.data!.buffer.asUint8List());

          return EpicScrollView(
            child: Padding(
              padding: const EdgeInsets.symmetric(
                horizontal: 16,
                vertical: 12,
              ),
              child: Center(
                child: ConstrainedBox(
                  constraints: BoxConstraints(maxWidth: 750),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.center,
                    children: [
                      Text(
                        AppLocalizations.of(context)!.appTitle,
                        style: Theme.of(context).textTheme.headlineMedium,
                      ),
                      FutureBuilder<String>(
                        future: getFriendlyPackageVersion(),
                        builder: (context, snapshot) => Text(snapshot.data ?? ''),
                      ),
                      const SizedBox(height: 8),
                      Text(
                        AppLocalizations.of(context)!.copyrightNotice,
                        textAlign: TextAlign.center,
                      ),
                      const SizedBox(height: 24),
                      HtmlWidget(
                        text,
                        onTapUrl: (String url) => launchUrl(Uri.parse(url)),
                        customStylesBuilder: _createCustomHtmlStyle,
                      ),
                    ],
                  ),
                ),
              ),
            ),
          );
        },
      ),
    );
  }

  /// Given an HTML [element] return a map of style settings to apply.
  StylesMap? _createCustomHtmlStyle(dom.Element element) {
    if (element.localName == 'a') {
      // Use the primary color for link underline instead of default text color
      final Color primaryColor = Theme.of(context).colorScheme.primary;

      // Convert to hex string, dropping the alpha component
      final int rgbColor = primaryColor.value & 0xffffff;
      final String rgbHexColor = rgbColor.toRadixString(16).padLeft(6, '0');

      return {'text-decoration-color': '#$rgbHexColor'};
    }

    return null;
  }
}
