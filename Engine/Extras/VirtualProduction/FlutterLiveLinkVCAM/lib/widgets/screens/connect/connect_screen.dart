// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/localizations.dart';
import 'package:epic_common/theme.dart';
import 'package:epic_common/unreal_beacon.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:logging/logging.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../../models/net/connection_enum.dart';
import '../../../models/net/engine_connection_manager.dart';
import '../../../models/settings/vcam_settings.dart';
import '../../../uri.dart';
import '../../../util/net_utilities.dart';
import '../../elements/settings_button.dart';
import '../streaming/streaming_screen.dart';
import 'manual_connect_dialog.dart';
import 'stream_picker_dialog.dart';

final _log = Logger('ConnectScreen');

class ConnectScreen extends StatefulWidget {
  const ConnectScreen({super.key});

  static const String route = '/connect';

  @override
  State<StatefulWidget> createState() => _ConnectScreenState();
}

class _ConnectScreenState extends State<ConnectScreen> {
  @override
  Widget build(BuildContext context) {
    final settings = Provider.of<VCamSettings>(context);

    return Scaffold(
      backgroundColor: UnrealColors.gray14,
      resizeToAvoidBottomInset: false,
      appBar: const _ConnectToolbar(),
      body: SafeArea(
        top: false,
        bottom: false,
        child: PreferenceBuilder<EngineConnectionData>(
          preference: settings.lastConnection,
          builder: (context, lastConnection) => EpicConnectScreen<VcamBeaconData, EngineConnectionData>(
            createBeacon: (context, onBeaconFailure) => UnrealEngineBeacon(
              context: context,
              config: unrealEngineBeaconConfig,
              onBeaconFailure: onBeaconFailure,
            ),
            mostRecentTileBuilder: (context) => lastConnection.bIsValid
                ? ConnectionTile(
                    onTap: () => _onConnectionSelected(lastConnection),
                    primaryText: _makeConnectionTitle(lastConnection),
                    secondaryText: lastConnection.name,
                    emphasisText: EpicCommonLocalizations.of(context)!.connectScreenMostRecentButtonLabel,
                    fillColor: Theme.of(context).colorScheme.primary,
                  )
                : null,
            beaconTileBuilder: (BuildContext context, dynamic beaconResponse) {
              if (!(beaconResponse is VcamBeaconResponse)) {
                return null;
              }

              final connectionData = EngineConnectionData.fromBeaconResponse(beaconResponse);

              return ConnectionTile(
                onTap: () => _onConnectionSelected(connectionData),
                primaryText: _makeConnectionTitle(connectionData),
                secondaryText: connectionData.name,
                emphasisText: beaconResponse.additionalData.bCanStream ? null : 'Unavailable\n(Not Streaming)',
              );
            },
            onManualConnectPressed: _onManualConnectPressed,
          ),
        ),
      ),
    );
  }

  /// Make the user-facing title for the given [connectionData].
  String _makeConnectionTitle(EngineConnectionData connectionData) =>
      '${connectionData.pixelStreamingAddress.address}:${connectionData.pixelStreamingPort}';

  /// Called when the user selects [connectionData] to which to connect.
  void _onConnectionSelected(EngineConnectionData connectionData, {String? streamer}) async {
    if (!mounted) {
      return;
    }

    final connectionManager = Provider.of<EngineConnectionManager>(context, listen: false);

    // Show the connection spinner dialog
    bool bSpinnerPopped = false;
    showConnectionDialog(
      context,
      connectionName: _makeConnectionTitle(connectionData),
    ).then((bool? bCancelled) {
      bSpinnerPopped = true;
      if (bCancelled == true) {
        connectionManager.disconnect();
      }
    });

    // Wait to connect
    final EngineConnectionResult result = await connectionManager.connect(connectionData, streamer: streamer);

    if (!bSpinnerPopped) {
      Navigator.of(context).pop();
    }

    switch (result) {
      case EngineConnectionResult.success:
        // Proceed to main screen
        Navigator.of(context).pushReplacementNamed(StreamingScreen.route);
        break;

      case EngineConnectionResult.needsStreamer:
        // Prompt the user to pick a stream
        String? streamer = await showStreamPicker(context, connectionManager.streamers);

        if (streamer == null) {
          // User cancelled, so cancel the connection
          _log.info('User cancelled connection while waiting for streamer selection');
          connectionManager.disconnect();
          return;
        }

        // Start the connection process again with the new streamer
        _onConnectionSelected(connectionData, streamer: streamer);
        break;

      case EngineConnectionResult.cancelled:
        // User cancelled, so no need to do anything
        break;

      default:
        // Show reason for failure
        final String connectionName =
            '${connectionData.pixelStreamingAddress.address}:${connectionData.pixelStreamingPort}';
        final bool? bShouldRetry = await showConnectionErrorDialog(context, connectionName: connectionName);

        if (bShouldRetry == true) {
          _onConnectionSelected(connectionData, streamer: streamer);
        }
        break;
    }
  }

  /// Show the form for entering manual connection data.
  void _onManualConnectPressed() async {
    final EngineConnectionData? connectionData = await showManualConnectForm(context);

    if (connectionData == null || !mounted) {
      return;
    }

    _onConnectionSelected(connectionData);
  }
}

class _ConnectToolbar extends StatelessWidget implements PreferredSizeWidget {
  const _ConnectToolbar();

  @override
  Size get preferredSize => const Size.fromHeight(48);

  @override
  Widget build(BuildContext context) {
    final textStyle = TextStyle(
      fontFamily: 'Droid Sans Mono',
      fontSize: 14,
      height: 1.2,
      fontFeatures: [
        FontFeature.tabularFigures(),
      ],
    );

    final settings = Provider.of<VCamSettings>(context);
    return PreferenceBuilder(
      preference: settings.bShowInfoBar,
      builder: (context, bShowInfoBar) => Stack(
        children: [
          if (bShowInfoBar)
            Align(
              alignment: Alignment.center,
              child: Padding(
                padding: EdgeInsets.only(left: 16, right: 48),
                child: DefaultTextStyle(
                  style: textStyle,
                  child: Center(
                    child: SingleChildScrollView(
                      scrollDirection: Axis.horizontal,
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          const TimecodeManagerSourceIcon(size: 16),
                          const SizedBox(width: 4),
                          const TimecodeDisplay(errorColor: null),
                        ],
                      ),
                    ),
                  ),
                ),
              ),
            ),
          Align(
            key: const Key('SettingsButton'),
            alignment: Alignment.centerRight,
            child: Padding(
              padding: EdgeInsets.only(right: 24),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  EpicIconButton(
                    tooltipMessage: AppLocalizations.of(context)!.settingsHelpButtonLabel,
                    iconPath: 'packages/epic_common/assets/icons/help.svg',
                    onPressed: () => launchUrl(Uri.parse(helpUri)),
                  ),
                  SettingsButton(),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
