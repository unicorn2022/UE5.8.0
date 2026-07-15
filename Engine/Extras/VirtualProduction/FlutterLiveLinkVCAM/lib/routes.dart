// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/material.dart';

import 'widgets/screens/connect/connect_screen.dart';
import 'widgets/screens/connect/reconnect_screen.dart';
import 'widgets/screens/settings/eula/eula_screen.dart';
import 'widgets/screens/streaming/streaming_screen.dart';

/// Contains data about a route in the app
class RouteData {
  const RouteData({required this.createScreen});

  /// A function to create the screen widget for the route.
  final Widget Function(BuildContext context) createScreen;

  /// A list of all routes in the app.
  static final Map<String, RouteData> allRoutes = {
    StreamingScreen.route: RouteData(createScreen: (context) => const StreamingScreen()),
    EulaScreen.route: RouteData(createScreen: (context) => const EulaScreen()),
    ConnectScreen.route: RouteData(createScreen: (context) => const ConnectScreen()),
    ReconnectScreen.route: RouteData(createScreen: (context) => const ReconnectScreen()),
  };
}

final Map<String, String> routes = {};
