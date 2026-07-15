// Copyright Epic Games, Inc. All Rights Reserved.

import 'package:flutter/widgets.dart';

import '../native/video_view_controller.dart';

/// Displays a real-time stream of a WebRTC video track.
class RtcVideoView extends StatelessWidget {
  const RtcVideoView(
    this.controller, {
    this.placeholder,
    Key? key,
  }) : super(key: key);

  /// The video view controller which communicates with the native plugin.
  final RtcVideoViewController controller;

  /// Builder for a widget to show when the video isn't available.
  final WidgetBuilder? placeholder;

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<int>(
      future: controller.textureId,
      builder: (context, textureIdSnapshot) {
        if (!textureIdSnapshot.hasData) {
          return _makePlaceholder(context);
        }

        return Texture(textureId: textureIdSnapshot.data!);
      },
    );
  }

  /// Make the placeholder widget.
  Widget _makePlaceholder(BuildContext context) => placeholder != null ? placeholder!(context) : const SizedBox();
}
