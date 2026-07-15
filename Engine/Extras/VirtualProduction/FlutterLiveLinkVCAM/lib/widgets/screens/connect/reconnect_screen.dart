// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:logging/logging.dart';
import 'package:provider/provider.dart';

import '../../../models/net/connection_enum.dart';
import '../../../models/net/engine_connection_manager.dart';
import '../../../util/net_utilities.dart';
import '../streaming/streaming_screen.dart';
import 'connect_screen.dart';
import 'stream_picker_dialog.dart';

final _log = Logger('ReconnectScreen');

/// Screen shown when reconnecting to the engine.
class ReconnectScreen extends StatefulWidget {
  const ReconnectScreen({Key? key}) : super(key: key);

  static const String route = '/reconnect';

  @override
  State<StatefulWidget> createState() => _ReconnectScreenState();
}

class _ReconnectScreenState extends State<ReconnectScreen> {
  @override
  void initState() {
    super.initState();

    WidgetsBinding.instance.addPostFrameCallback((_) => _reconnect());
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Theme.of(context).colorScheme.surface,
    );
  }

  void _reconnect({String? streamer}) async {
    final connectionManager = Provider.of<EngineConnectionManager>(context, listen: false);

    // Show the connection spinner dialog
    bool bSpinnerPopped = false;
    showConnectionDialog(
      context,
      title: AppLocalizations.of(context)!.reconnectTitle,
      message: AppLocalizations.of(context)!.reconnectMessage,
    ).then((bool? bCancelled) {
      bSpinnerPopped = true;
      if (bCancelled == true) {
        connectionManager.disconnect();
      }
    });

    // Wait to connect
    final EngineConnectionResult result = await connectionManager.reconnect(streamer: streamer);

    if (!mounted) {
      return;
    }

    if (!bSpinnerPopped) {
      Navigator.of(context).pop();
    }

    switch (result) {
      case EngineConnectionResult.success:
        _log.info('Reconnected successfully');

        // Proceed to main screen
        Navigator.of(context).pushReplacementNamed(StreamingScreen.route);
        return;

      case EngineConnectionResult.needsStreamer:
        // Prompt the user to pick a stream
        final String? streamer = await showStreamPicker(context, connectionManager.streamers);

        if (streamer != null) {
          // Start the connection process again with the new streamer
          _reconnect(streamer: streamer);
        }

        // Otherwise, user cancelled, so fall through to connect screen

        break;

      case EngineConnectionResult.cancelled:
        _log.info('User cancelled reconnect attempt');
        // Fall through and return to connect screen
        break;

      default:
        // Connection failed for some other reason, so show reason for failure and retry prompt
        final EngineConnectionData? connectionData = connectionManager.lastConnection;

        if (connectionData == null) {
          _log.warning('Tried to reconnect but there was no last connection data. This shouldn\'t be possible.');
          Navigator.of(context).pushNamedAndRemoveUntil(ConnectScreen.route, (route) => false);
          return;
        }

        final String connectionName =
            '${connectionData.pixelStreamingAddress.address}:${connectionData.pixelStreamingPort}';

        final bool? bShouldRetry = await showConnectionErrorDialog(
          context,
          connectionName: connectionName,
          title: AppLocalizations.of(context)!.reconnectFailedTitle,
          message: AppLocalizations.of(context)!.reconnectFailedMessage(connectionName),
        );

        if (bShouldRetry == true) {
          _log.info('User initiated another reconnect attempt');
          _reconnect(streamer: streamer);
          return;
        }

        _log.info('User declined another reconnect attempt');
        break;
    }

    // Fallback: return to connect screen
    Navigator.of(context).pushNamedAndRemoveUntil(ConnectScreen.route, (route) => false);
  }
}
