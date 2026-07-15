// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:isolate';
import 'dart:ui';

import 'package:epic_common/localizations.dart';
import 'package:epic_common/logging.dart';
import 'package:epic_common/preferences.dart';
import 'package:epic_common/theme.dart';
import 'package:epic_common/third_party_notice.dart';
import 'package:epic_common/timecode.dart';
import 'package:epic_common/utilities/version.dart';
import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:flutter_tentacle/tentacle.dart';
import 'package:logging/logging.dart';
import 'package:provider/provider.dart';
import 'package:streaming_shared_preferences/streaming_shared_preferences.dart';

import 'models/net/engine_connection_manager.dart';
import 'models/settings/vcam_settings.dart';
import 'routes.dart';
import 'util/nav_key.dart';
import 'webrtc/api/peer_connection_api.dart';
import 'widgets/screens/connect/connect_screen.dart';

final _log = Logger('Main');

void main() async {
  Logging.instance.initialize('live_link_vcam');

  final asyncLog = Logger('Async');

  // Catch any asynchronous errors
  runZonedGuarded(
    () => initAndRunApp(),
    (error, stack) {
      asyncLog.severe(error, error, stack);
    },
  );
}

/// Set up the app and run it. Encapsulated so we can wrap it in [runZonedGuarded] in [main] for better error handling.
void initAndRunApp() async {
  WidgetsFlutterBinding.ensureInitialized();

  _log.info(await getVerbosePackageVersion());

  final flutterLog = Logger('Flutter');
  final isolateLog = Logger('Isolate');

  await preloadShaders();

  // Catch any errors originating from or otherwise passed on by Flutter
  FlutterError.onError = (FlutterErrorDetails details) {
    flutterLog.severe(details.exception, details.exception, details.stack);
  };

  // Catch errors outside the Flutter context
  Isolate.current.addErrorListener(RawReceivePort((errorStackPair) {
    isolateLog.severe(errorStackPair.first, errorStackPair.first, StackTrace.fromString(errorStackPair.second));
  }).sendPort);

  // Settings to improve latency
  WebRtcPeerConnectionApi.instance.host.setFieldTrials({
    "WebRTC-MaxPacketBufferSize": "8192",
    "WebRTC-ForcePlayoutDelay": "min_ms:1,max_ms:1",
    "WebRTC-ZeroPlayoutDelay": "min_pacing:4ms,max_decode_queue_size:1",
  });

  // Load user settings
  final preferences = await StreamingSharedPreferences.instance;

  // Run the app itself
  runApp(VcamApp(
    preferences: preferences,
    onShutdown: shutdown,
  ));
}

/// Preload any shaders we expect to use regularly.
Future preloadShaders() async {
  await EpicCommonWidgets.preloadShaders();
}

/// Unload any preloaded shaders.
void unloadShaders() {
  EpicCommonWidgets.unloadShaders();
}

/// Called when the app shuts down.
void shutdown() {
  unloadShaders();
  TentaclePlugin.shutdown();
}

class VcamApp extends StatefulWidget {
  const VcamApp({
    super.key,
    required this.preferences,
    required this.onShutdown,
  });

  /// Preferences manager to use for the app.
  final StreamingSharedPreferences preferences;

  /// Called when the app shuts down.
  final Function() onShutdown;

  @override
  State<VcamApp> createState() => _VcamAppState();
}

class _VcamAppState extends State<VcamApp> with WidgetsBindingObserver {
  late final PreferencesBundle _preferenceBundle;

  @override
  void initState() {
    super.initState();

    _preferenceBundle = PreferencesBundle(widget.preferences, TransientSharedPreferences());

    _log.info('App initialized');
  }

  @override
  Widget build(BuildContext context) {
    var routes = {for (var entry in RouteData.allRoutes.entries) entry.key: entry.value.createScreen};

    routes['/'] = routes[ConnectScreen.route]!;

    return MultiProvider(
      providers: [
        Provider<PreferencesBundle>(create: (_) => _preferenceBundle),
        Provider(
          create: (_) => TentacleDeviceManager(),
          dispose: (_, provider) => provider.dispose(),
        ),
        Provider(
          create: (context) {
            final manager = TimecodeManager(context: context, preferences: _preferenceBundle);
            manager.registerSource(TentacleTimecodeSource());
            manager.initialize();
            return manager;
          },
          dispose: (_, provider) => provider.dispose(),
        ),
        Provider<VCamSettings>(create: (_) => VCamSettings(_preferenceBundle)),
        Provider<EngineConnectionManager>(
          create: (context) => EngineConnectionManager(context),
          dispose: (_, manager) => manager.dispose(),
        ),
        Provider<ThirdPartyNoticeManifest>(
          create: (context) => ThirdPartyNoticeManifest(assetBundle: DefaultAssetBundle.of(context)),
        )
      ],
      child: Stack(
        alignment: Alignment.topLeft,
        children: [
          MaterialApp(
            theme: UnrealTheme.makeThemeData(),
            localizationsDelegates: [
              ...EpicCommonLocalizations.localizationsDelegates,
              ...TentacleLocalizations.localizationsDelegates,
              ...AppLocalizations.localizationsDelegates,
            ],
            navigatorKey: rootNavigatorKey,
            onGenerateRoute: (RouteSettings settings) {
              Widget Function(BuildContext)? pageFunction = routes[settings.name];
              if (pageFunction != null) {
                return PageRouteBuilder(
                  settings: settings,
                  // Add safe area on all pages to avoid overlapping the status bar
                  pageBuilder: (context, animation, secondaryAnimation) => Container(
                    color: Theme.of(context).colorScheme.background,
                    child: SafeArea(
                      left: false,
                      right: false,
                      bottom: false,
                      top: true,
                      child: pageFunction(context),
                    ),
                  ),
                  transitionDuration: Duration.zero,
                );
              }

              return MaterialPageRoute(builder: (_) => Text("Invalid page ${settings.name ?? "(null)"}"));
            },
          ),
          // Workaround for UE-350056 / https://github.com/flutter/flutter/issues/175606
          SizedBox(
            width: 1,
            height: 1,
            child: AbsorbPointer(),
          ),
        ],
      ),
    );
  }

  @override
  Future<AppExitResponse> didRequestAppExit() async {
    final response = await super.didRequestAppExit();

    if (response == AppExitResponse.exit) {
      widget.onShutdown();
    }

    return response;
  }
}
