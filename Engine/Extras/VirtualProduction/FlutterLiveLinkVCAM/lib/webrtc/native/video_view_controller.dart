// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:ui';

import 'package:flutter/foundation.dart';

import '../../models/native/native_object.dart';
import '../../models/native/native_object_manager.dart';
import '../api/video_view_controller_api.dart';
import 'media_stream_track.dart';

/// Event generated when the size of frame rendered in an [RtcVideoView] changes.
class RtcVideoViewFrameSizeChangedEvent {
  const RtcVideoViewFrameSizeChangedEvent(this.width, this.height);

  /// The new width of the frame.
  final int width;

  /// The new height of the frame.
  final int height;
}

/// Controller which manages the state of an [RtcVideoView] and communicates with the native plugin.
class RtcVideoViewController extends NativeObject {
  RtcVideoViewController({
    RtcVideoViewControllerApi? api,
  }) : this.api = api ?? RtcVideoViewControllerApi.instance;

  /// The API used to communicate with the host language.
  final RtcVideoViewControllerApi api;

  /// The video track to display.
  RtcMediaStreamTrack? _track;

  /// Stream controller for [size].
  final _size = ValueNotifier(Size.zero);

  /// Whether the video view has a frame ready to display.
  final _hasFirstFrame = ValueNotifier(false);

  /// A stream that broadcasts whenever the stream size changes.
  ValueListenable<Size> get size => _size;

  /// Whether the video view has a frame ready to display.
  ValueListenable<bool> get hasFirstFrame => _hasFirstFrame;

  /// The ID of the texture containing the streamed video.
  Future<int> get textureId => whenInitialized.then((_) => api.host.getTextureId(id));

  /// The video track to display.
  RtcMediaStreamTrack? get track => _track;

  void set track(RtcMediaStreamTrack? newTrack) {
    _track = newTrack;
    _size.value = Size.zero;
    api.host.setTrack(id, _track?.id ?? NativeObject.invalidId);
  }

  @override
  NativeObjectManager<NativeObject> get manager => api.manager;

  /// Clear the video view's rendered image.
  void clear() {
    _hasFirstFrame.value = false;

    if (bInitialized) {
      api.host.clear(id);
    }
  }

  /// Called when the associated video view's stream changes the size of its frame.
  void onFrameSizeChanged(int width, int height) => _size.value = Size(width.toDouble(), height.toDouble());

  /// Called when the VideoViewController with the view has rendered its first frame since it was created or cleared
  /// (whichever is the most recent).
  void onFirstFrameRendered() => _hasFirstFrame.value = true;

  @override
  Future<int> internalInit() => api.host.create();

  @override
  Future<void> internalDispose() async {
    if (bInitialized) {
      await api.host.dispose(id);
    }

    _size.dispose();
    _hasFirstFrame.dispose();
  }
}
