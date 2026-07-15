// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:epic_common/widgets.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_gen/gen_l10n/app_localizations.dart';
import 'package:live_link_vcam/webrtc/data/pixel_streaming_to_client_message.dart';
import 'package:logging/logging.dart';

import '../../models/net/webrtc_client.dart';
import '../../util/numeric_utils.dart';
import '../data/pixel_streaming_to_streamer_message.dart';

final _log = Logger('RtcTouchHandler');

/// Handles touch events by sending them over the WebRTC client's data channel.
class RtcTouchHandler extends StatefulWidget {
  const RtcTouchHandler({
    super.key,
    required this.client,
    required this.builder,
  });

  /// The WebRTC client used to communicate input.
  final UnrealWebRtcClient client;

  /// A builder function that creates the inner contents, passing a build [context] and a [boundsKey] which must be
  /// provided as the key of the child widget whose bounds will define the range for valid touches.
  final Function(BuildContext context, Key boundsKey) builder;

  @override
  State<RtcTouchHandler> createState() => _RtcTouchHandlerState();
}

class _RtcTouchHandlerState extends State<RtcTouchHandler> with SingleTickerProviderStateMixin {
  /// Ticker used to send pointer movement updates every frame.
  late final Ticker _ticker;

  /// Key passed to the child whose bounds define the touch bounds.
  final _boundsKey = GlobalKey();

  /// Map from pointer ID (non-reused ID sent from pointer events) to active touch data.
  /// Note that pointer ID is different than touch ID, which is the reusable ID that we send to the streamer.
  final Map<int, QuantizedTouchData> _touchesByPointerId = {};

  /// Set of touch IDs that are currently in use.
  final Set<int> _touchIdsInUse = {};

  /// Set of pointer IDs for touches that have moved this frame.
  final Set<int> _pointerIdsMovedThisFrame = {};

  /// Current subscription to command messages from the data channel.
  StreamSubscription? _commandMessageSubscription;

  @override
  void initState() {
    super.initState();

    _ticker = createTicker(_onTick);
    _ticker.start();

    _commandMessageSubscription = widget.client.listenToDataChannel(
      PixelStreamingToClientMessageKind.command,
      _onRemoteCommand,
    );
  }

  @override
  void didUpdateWidget(covariant RtcTouchHandler oldWidget) {
    if (oldWidget.client != widget.client) {
      _commandMessageSubscription?.cancel();

      _commandMessageSubscription = widget.client.listenToDataChannel(
        PixelStreamingToClientMessageKind.command,
        _onRemoteCommand,
      );
    }

    super.didUpdateWidget(oldWidget);
  }

  @override
  void dispose() {
    _ticker.dispose();

    _commandMessageSubscription?.cancel();

    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Listener(
      behavior: HitTestBehavior.opaque,
      onPointerDown: _onPointerDown,
      onPointerMove: _onPointerMove,
      onPointerUp: _onPointerUp,
      onPointerCancel: _onPointerCancel,
      child: widget.builder(context, _boundsKey),
    );
  }

  /// Called when a pointer makes contact with the screen.
  void _onPointerDown(PointerDownEvent event) {
    final int touchId = _getFreeTouchId();
    if (!NumericUtils.isValid8BitUInt(touchId)) {
      return;
    }

    _touchIdsInUse.add(touchId);

    final QuantizedTouchData? touchData = _quantizeTouchData(
      touchId: touchId,
      event: event,
    );

    if (touchData == null) {
      return;
    }

    _touchesByPointerId[event.pointer] = touchData;
    _sendTouches(PixelStreamingToStreamerMessageKind.touchStart, [touchData]);
  }

  /// Called when a pointer moves.
  void _onPointerMove(PointerMoveEvent event) {
    final QuantizedTouchData? trackedTouch = _touchesByPointerId[event.pointer];

    if (trackedTouch == null) {
      return;
    }

    final QuantizedTouchData? touchData = _quantizeTouchData(
      touchId: trackedTouch.id,
      event: event,
    );

    if (touchData == null) {
      return;
    }

    // Don't send a pointer move event if the quantized data hasn't changed
    if (touchData.x == trackedTouch.x && touchData.y == trackedTouch.y) {
      return;
    }

    _touchesByPointerId[event.pointer] = touchData;
    _pointerIdsMovedThisFrame.add(event.pointer);
  }

  /// Called when a pointer stops contacting the screen.
  void _onPointerUp(PointerUpEvent event) {
    _forgetTouch(event);
  }

  /// Called when a pointer's input is no longer directed to the receiver.
  void _onPointerCancel(PointerCancelEvent event) {
    _forgetTouch(event);
  }

  /// Called every tick.
  void _onTick(Duration elapsed) {
    if (_pointerIdsMovedThisFrame.isEmpty) {
      return;
    }

    // Make a list of data for touches that moved this frame
    final List<QuantizedTouchData> movedTouches = [];
    for (final int pointerId in _pointerIdsMovedThisFrame) {
      final QuantizedTouchData? touch = _touchesByPointerId[pointerId];
      assert(touch != null);

      movedTouches.add(touch!);
    }

    _pointerIdsMovedThisFrame.clear();

    _sendTouches(PixelStreamingToStreamerMessageKind.touchMove, movedTouches);
  }

  /// Forget one of the tracked touches by sending a final move + end update, then releasing its touch ID and any other
  /// cached data.
  void _forgetTouch(PointerEvent event) {
    final QuantizedTouchData? trackedTouch = _touchesByPointerId.remove(event.pointer);
    if (trackedTouch == null) {
      return;
    }

    final QuantizedTouchData? touchData = _quantizeTouchData(
      touchId: trackedTouch.id,
      event: event,
    );

    if (touchData == null) {
      return;
    }

    // Indicate that the touch has ended
    final touchList = [touchData];
    _sendTouches(PixelStreamingToStreamerMessageKind.touchEnd, touchList);

    // Forget the touch and remove it from the moved list, as we already sent its final position/force/etc.
    _pointerIdsMovedThisFrame.remove(event.pointer);
    _touchIdsInUse.remove(trackedTouch.id);
  }

  /// Get the first touch ID that hasn't been used.
  int _getFreeTouchId() {
    int touchId = 0;
    while (_touchIdsInUse.contains(touchId)) {
      ++touchId;
    }

    const int maxTouchId = 255;
    if (touchId > maxTouchId) {
      _log.severe('Exceeded ${maxTouchId + 1} simultaneous touches. IDs are likely not being reused');
    }

    return touchId;
  }

  /// Create quantized touch data from a raw pointer [event] and an assigned [touchId].
  QuantizedTouchData? _quantizeTouchData({
    required int touchId,
    required PointerEvent event,
  }) {
    // Max size of a 16-bit unsigned integer
    const int positionMax = (1 << 16) - 1;

    // Max size of an 8-bit unsigned integer
    const int forceMax = (1 << 8) - 1;

    final RenderBox? boundsBox = _boundsKey.currentContext?.findRenderObject() as RenderBox?;
    if (boundsBox == null) {
      _log.severe('No bounds found, so touch input can\'t be quantized. No input will be sent.');
      return null;
    }

    if (boundsBox.size.shortestSide == 0) {
      _log.severe('Invalid bounds found, so touch input can\'t be quantized. No input will be sent.');
      return null;
    }

    final Offset localPosition = boundsBox.globalToLocal(event.position);
    final Offset normalizedPosition = localPosition.scale(1 / boundsBox.size.width, 1 / boundsBox.size.height);

    // If we're out of range, early out and send default values
    if (normalizedPosition.dx < 0 ||
        normalizedPosition.dx > 1 ||
        normalizedPosition.dy < 0 ||
        normalizedPosition.dy > 1) {
      return QuantizedTouchData(id: touchId, x: positionMax, y: positionMax, force: 0, bInRange: false);
    }

    final double pressureRange = event.pressureMax - event.pressureMin;
    final double normalizedPressure;

    if (pressureRange > 0) {
      normalizedPressure = (event.pressure - event.pressureMin) / pressureRange;
    } else {
      normalizedPressure = 0;
    }

    return QuantizedTouchData(
      id: touchId,
      x: (normalizedPosition.dx * positionMax).floor(),
      y: (normalizedPosition.dy * positionMax).floor(),
      force: (normalizedPressure * forceMax).floor(),
      bInRange: true,
    );
  }

  /// Send the state of all active touches via WebRTC.
  void _sendTouches(PixelStreamingToStreamerMessageKind messageKind, Iterable<QuantizedTouchData> touches) {
    final message = StreamerTouchMessage(touches: touches);
    widget.client.sendOnDataChannel(messageKind, message);
  }

  /// Called when a command is received from the WebRTC streamer.
  void _onRemoteCommand(dynamic message) {
    if (!(message is ClientCommandMessage)) {
      return;
    }

    switch (message.json['command']) {
      case 'onScreenKeyboard':
        // Keyboard entry to the currently focused textbox in UE
        if (message.json['showOnScreenKeyboard'] == true) {
          _showKeyboardPrompt(defaultValue: message.json['contents']);
        }
        break;

      case 'stringPrompt':
        // Generic prompt for a string, which must send back the ID to route the response to its requestor
        _showKeyboardPrompt(
          defaultValue: message.json['defaultValue'],
          requestId: message.json['requestId'],
          promptTitle: message.json['promptTitle'],
        );
        break;
    }
  }

  /// Show the input prompt for the on-screen keyboard with the given [defaultValue] pre-filled.
  /// If a [requestId] is provided, this will be treated as a manual text prompt request, which sends back a different
  /// type of message containing the ID.
  /// If a [promptTitle] is provided, it will be used as the dialog's title. Otherwise, a generic title will be used.
  void _showKeyboardPrompt({String? defaultValue = null, int? requestId = null, String? promptTitle = null}) async {
    final TextInputModalDialogResult<String>? result = await GenericModalDialogRoute.showDialog(
      context: context,
      builder: (context) => StringTextInputModalDialog(
        title: promptTitle?.isNotEmpty == true
            ? promptTitle!
            : AppLocalizations.of(context)!.remoteKeyboardInputModalTitle,
        initialValue: defaultValue,
      ),
    );

    final String? entry = result?.value;
    final bool bCancelled = (result?.action != TextInputModalDialogAction.apply);

    if (requestId == null) {
      // If input is going to an Unreal textbox, there's no concept of cancelling, so just don't send a response at all
      // unless we received a string from the user
      if (!bCancelled && entry != null) {
        widget.client.sendOnDataChannel(
          PixelStreamingToStreamerMessageKind.textboxEntry,
          StreamerTextboxEntryMessage(entry: entry),
        );
      }
    } else {
      // If input is going to a string prompt request, send the result no matter what
      widget.client.sendOnDataChannel(
        PixelStreamingToStreamerMessageKind.stringPrompt,
        StreamerStringPromptMessage(
          requestId: requestId,
          bCancelled: bCancelled,
          entry: entry,
        ),
      );
    }
  }
}
