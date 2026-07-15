// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:live_link_vcam/gamepad/api/gamepad_api.dart';
import 'package:live_link_vcam/gamepad/api/gamepad_api.g.dart';

/// Temporary screen that displays gamepad input.
class WipGamepadTest extends StatefulWidget {
  const WipGamepadTest({super.key});

  @override
  State<StatefulWidget> createState() => _WipGamepadTestState();
}

class _WipGamepadTestState extends State<WipGamepadTest> {
  /// Subscriptions to cancel on dispose.
  final List<StreamSubscription> _subscriptions = [];

  /// List of gamepad button events.
  final List<String> _events = [];

  @override
  void initState() {
    super.initState();

    _subscriptions.addAll([
      GamepadApi.instance.inputEvents.listen(_onGamepadInput),
      GamepadApi.instance.connectedEvents.listen(_onGamepadChanged),
      GamepadApi.instance.disconnectedEvents.listen(_onGamepadChanged),
    ]);
  }

  @override
  void dispose() {
    _subscriptions.forEach((subscription) => subscription.cancel());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final controllerId = GamepadApi.instance.connectedIds.firstOrNull ?? 0;

    return Scaffold(
      body: SafeArea(
        child: Center(
          child: Text(
            'Connected: ${GamepadApi.instance.connectedIds.join(', ')}\n'
            'LX: ${GamepadApi.instance.getAxisValue(controllerId, GamepadInput.thumbstickLeftX)}\n'
            'LY: ${GamepadApi.instance.getAxisValue(controllerId, GamepadInput.thumbstickLeftY)}\n'
            'RX: ${GamepadApi.instance.getAxisValue(controllerId, GamepadInput.thumbstickRightX)}\n'
            'RY: ${GamepadApi.instance.getAxisValue(controllerId, GamepadInput.thumbstickRightY)}\n'
            'L2: ${GamepadApi.instance.getAxisValue(controllerId, GamepadInput.triggerAxisLeft)}\n'
            'R2: ${GamepadApi.instance.getAxisValue(controllerId, GamepadInput.triggerAxisRight)}\n'
            '${_events.join('\n')}',
          ),
        ),
      ),
    );
  }

  void _onGamepadInput(GamepadInputEvent event) {
    if (event.type == GamepadInputType.button) {
      _events.add('${event.input.name} -> ${event.value.toInt()}');

      if (_events.length > 10) {
        _events.removeAt(0);
      }
    }

    setState(() {});
  }

  void _onGamepadChanged(int gamepadId) {
    setState(() {});
  }
}
