// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:convert';
import 'dart:typed_data';

import 'package:meta/meta.dart';
import 'package:vector_math/vector_math.dart';

import '../../util/numeric_utils.dart';

/// ID to send for each type of message we can send to the streamer via pixel streaming.
enum PixelStreamingToStreamerMessageKind {
  /// Request a new key frame.
  iFrameRequest(0),

  /// Request control of the stream quality.
  requestQualityControl(1),

  /// Request a frame rate for the stream.
  fpsRequest(2),

  /// Request an average bitrate for the stream.
  averageBitrateRequest(3),

  /// Request that the streamer start streaming video.
  startStreaming(4),

  /// Request that the streamer stop streaming video.
  stopStreaming(5),

  /// Request a report of the connection's latency.
  latencyTest(6),

  /// Request the streamer's initial Pixel Streaming settings.
  requestInitialSettings(7),

  /// Request an echo response.
  testEcho(8),

  /// Send interaction from the app's UI.
  uiInteraction(50),

  /// A JSON command.
  command(51),

  /// Textbox input to the active textbox.
  textboxEntry(52),

  /// An event when a key goes down on the keyboard.
  keyDown(60),

  /// An event when a key goes up on the keyboard.
  keyUp(61),

  /// An event when a key is pressed on the keyboard.
  keyPress(62),

  /// An event when the mouse enters the screen.
  mouseEnter(70),

  /// An event when the mouse leaves the screen.
  mouseLeave(71),

  /// An event when a mouse button is pressed.
  mouseDown(72),

  /// An event when a mouse button is released.
  mouseUp(73),

  /// An event when the mouse is moved.
  mouseMove(74),

  /// An event when the mouse wheel is scrolled.
  mouseWheel(75),

  /// An event when a touchscreen touch starts.
  touchStart(80),

  /// An event when a touchscreen touch ends.
  touchEnd(81),

  /// An event when a touchscreen touch moves.
  touchMove(82),

  /// An event when a button is pressed on a gamepad.
  gamepadButtonPressed(90),

  /// An event when a button is released on a gamepad.
  gamepadButtonReleased(91),

  /// An event when an analog stick position changes on a gamepad.
  gamepadAnalog(92),

  /// An event when a gamepad is connected to the app.
  gamepadConnected(93),

  /// An event when a gamepad is disconnected from the app.
  gamepadDisconnected(94),

  /// An event when the transform of the device changes in world space.
  transform(100),

  /// An event responding to a direct request for string input.
  stringPrompt(101);

  /// The value indicating the message type.
  final int id;

  const PixelStreamingToStreamerMessageKind(this.id);
}

/// Base class for messages sent to the streamer.
abstract class PixelStreamingToStreamerMessage {
  const PixelStreamingToStreamerMessage();

  /// Encode the message so it can be sent over the data channel.
  Uint8List encode(int id) {
    final Uint8List data = internalEncode();

    assert(data.isNotEmpty);

    data[0] = id;

    return data;
  }

  /// Encode the message data, if any, to a list of bytes.
  /// This must return at least 1 byte, and byte 0 is reserved for the message ID (which will be added automatically).
  @protected
  Uint8List internalEncode();
}

/// A message sending a command to the streamer.
class StreamerCommandMessage extends PixelStreamingToStreamerMessage {
  const StreamerCommandMessage({required this.command});

  /// The JSON command to send.
  final Object command;

  @override
  Uint8List internalEncode() {
    final jsonString = jsonEncode(command);

    final messageData = ByteData(jsonString.length * 2 + 3); // UTF-16 string + 8-bit message type + 16-bit length
    messageData.setUint16(1, jsonString.length);

    for (int charIndex = 0; charIndex < jsonString.codeUnits.length; ++charIndex) {
      messageData.setUint16(2 + (charIndex * 2), jsonString.codeUnits[charIndex]);
    }

    return Uint8List.view(messageData.buffer);
  }
}

/// A message sending a list of touches to the streamer.
class StreamerTouchMessage extends PixelStreamingToStreamerMessage {
  const StreamerTouchMessage({required this.touches});

  /// The list of touches to send.
  final Iterable<QuantizedTouchData> touches;

  @override
  Uint8List internalEncode() {
    // 2B x 2 for position + 1B for ID + 1B for force + 1B for bInRange
    const touchSize = 7;

    // 1B for message + 1B for touch count + touch sizes
    final messageData = ByteData(2 + (touchSize * touches.length));

    messageData.setUint8(1, touches.length);

    int byteOffset = 2;
    for (final QuantizedTouchData touch in touches) {
      messageData.setUint16(byteOffset, touch.x, Endian.little);
      messageData.setUint16(byteOffset + 2, touch.y, Endian.little);
      messageData.setUint8(byteOffset + 4, touch.id);
      messageData.setUint8(byteOffset + 5, touch.force);
      messageData.setUint8(byteOffset + 6, touch.bInRange ? 1 : 0);

      byteOffset += touchSize;
    }

    return Uint8List.view(messageData.buffer);
  }
}

/// A single touch sent as part of a [StreamerTouchMessage].
class QuantizedTouchData {
  QuantizedTouchData({
    required this.id,
    required this.x,
    required this.y,
    required this.force,
    required this.bInRange,
  })  : assert(NumericUtils.isValid8BitUInt(id)),
        assert(NumericUtils.isValid16BitUInt(x)),
        assert(NumericUtils.isValid16BitUInt(y)),
        assert(NumericUtils.isValid8BitUInt(force));

  /// 8-bit unique identifier for the touch.
  final int id;

  /// 16-bit quantized X position.
  final int x;

  /// 16-bit quantized Y position.
  final int y;

  /// 8-bit quantized touch force.
  final int force;

  /// Whether the touch is in range.
  final bool bInRange;
}

/// A message sending an AR transform update to the streamer.
class StreamerTransformMessage extends PixelStreamingToStreamerMessage {
  const StreamerTransformMessage({
    required this.transform,
    required this.timestamp,
  });

  /// The transform matrix received from the AR plugin.
  final Matrix4 transform;

  /// The timestamp in seconds.
  final double timestamp;

  @override
  Uint8List internalEncode() {
    // 1B for ID + 4x4x4B for float transform matrix + 8B for timestamp
    final messageData = ByteData(73);

    int byteOffset = 1;
    for (int columnIndex = 0; columnIndex < 4; ++columnIndex) {
      final Vector4 column = transform.getColumn(columnIndex);

      for (int rowIndex = 0; rowIndex < 4; ++rowIndex) {
        messageData.setFloat32(byteOffset, column[rowIndex], Endian.little);
        byteOffset += 4;
      }
    }

    messageData.setFloat64(byteOffset, timestamp, Endian.little);

    return Uint8List.view(messageData.buffer);
  }
}

/// A message sending text entered into a textbox to the streamer.
class StreamerTextboxEntryMessage extends PixelStreamingToStreamerMessage {
  const StreamerTextboxEntryMessage({required this.entry});

  /// The data the user entered.
  final String entry;

  @override
  Uint8List internalEncode() {
    // 1B for ID + 2B for string length + 2B per UTF-16 char
    final messageData = ByteData(3 + (entry.length * 2));

    messageData.setUint16(1, entry.length * 2, Endian.little);

    int byteOffset = 3;
    for (final int codeUnit in entry.codeUnits) {
      messageData.setUint16(byteOffset, codeUnit, Endian.little);

      byteOffset += 2;
    }

    return Uint8List.view(messageData.buffer);
  }
}

/// A message sending notification of a newly disconnected gamepad to the streamer.
class StreamerGamepadDisconnectedMessage extends PixelStreamingToStreamerMessage {
  const StreamerGamepadDisconnectedMessage({required this.controllerIndex});

  /// The Unreal-provided index of the controller that received the input.
  final int controllerIndex;

  @override
  Uint8List internalEncode() {
    // 1B for ID + 1B for controller index
    final messageData = ByteData(2);

    messageData.setUint8(1, controllerIndex);

    return Uint8List.view(messageData.buffer);
  }
}

/// A message sending a gamepad analog value to the streamer.
class StreamerGamepadAnalogMessage extends PixelStreamingToStreamerMessage {
  const StreamerGamepadAnalogMessage({
    required this.controllerIndex,
    required this.analogIndex,
    required this.value,
  });

  /// The Unreal-provided index of the controller that received the input.
  final int controllerIndex;

  /// The index of the analog input type received.
  final int analogIndex;

  /// The value of the input.
  final double value;

  @override
  Uint8List internalEncode() {
    // 1B for ID + 1B for controller ID + 1B for input ID + 8B for value
    final messageData = ByteData(11);

    messageData.setUint8(1, controllerIndex);
    messageData.setUint8(2, analogIndex);
    messageData.setFloat64(3, value, Endian.little);

    return Uint8List.view(messageData.buffer);
  }
}

/// A message sent when a gamepad button is pressed.
class StreamerGamepadButtonPressedMessage extends PixelStreamingToStreamerMessage {
  const StreamerGamepadButtonPressedMessage({
    required this.controllerIndex,
    required this.buttonIndex,
    required this.bIsRepeat,
  });

  /// The Unreal-provided index of the controller that received the input.
  final int controllerIndex;

  /// The index of the analog input type received.
  final int buttonIndex;

  /// Whether this was a repeat input.
  final bool bIsRepeat;

  @override
  Uint8List internalEncode() {
    // 1B for ID + 1B for controller ID + 1B for input ID + 1B for repeat
    final messageData = ByteData(4);

    messageData.setUint8(1, controllerIndex);
    messageData.setUint8(2, buttonIndex);
    messageData.setUint8(3, bIsRepeat ? 1 : 0);

    return Uint8List.view(messageData.buffer);
  }
}

/// A message sent when a gamepad button is released.
class StreamerGamepadButtonReleasedMessage extends PixelStreamingToStreamerMessage {
  const StreamerGamepadButtonReleasedMessage({
    required this.controllerIndex,
    required this.buttonIndex,
  });

  /// The Unreal-provided index of the controller that received the input.
  final int controllerIndex;

  /// The index of the analog input type received.
  final int buttonIndex;

  @override
  Uint8List internalEncode() {
    // 1B for ID + 1B for controller ID + 1B for input ID
    final messageData = ByteData(3);

    messageData.setUint8(1, controllerIndex);
    messageData.setUint8(2, buttonIndex);

    return Uint8List.view(messageData.buffer);
  }
}

/// A message sending a response to a string prompt request.
class StreamerStringPromptMessage extends PixelStreamingToStreamerMessage {
  const StreamerStringPromptMessage({
    required this.requestId,
    required this.bCancelled,
    this.entry,
  });

  /// The ID of the request to which this is a response.
  final int requestId;

  /// Whether the user cancelled the prompt.
  final bool bCancelled;

  /// The string the user entered, if any.
  final String? entry;

  @override
  Uint8List internalEncode() {
    final String entryOrEmpty = entry ?? '';

    // 1B for ID + 2B for requestID + 1B for bCancelled + 2B for string length + 2B per UTF-16 char
    final messageData = ByteData(6 + (entryOrEmpty.length * 2));

    messageData.setInt16(1, requestId, Endian.little);
    messageData.setUint8(3, bCancelled ? 1 : 0);

    messageData.setUint16(4, entryOrEmpty.length * 2, Endian.little);

    int byteOffset = 6;
    for (final int codeUnit in entryOrEmpty.codeUnits) {
      messageData.setUint16(byteOffset, codeUnit, Endian.little);

      byteOffset += 2;
    }

    return Uint8List.view(messageData.buffer);
  }
}
