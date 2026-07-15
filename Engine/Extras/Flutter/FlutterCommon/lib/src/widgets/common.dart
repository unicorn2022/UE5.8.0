// Copyright Epic Games, Inc. All Rights Reserved.

import 'epic_scroll_view.dart';

class EpicCommonWidgets {
  /// Preload any shaders required for widgets in the epic_common package.
  static Future preloadShaders() async {
    await EpicScrollView.preloadShaders();
  }

  /// Unload any shaders required for widgets in the epic_common package.
  static void unloadShaders() {
    EpicScrollView.unloadShaders();
  }
}
