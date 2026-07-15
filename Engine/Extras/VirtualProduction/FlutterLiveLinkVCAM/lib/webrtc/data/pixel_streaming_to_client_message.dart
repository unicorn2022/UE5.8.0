// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:convert';
import 'dart:typed_data';

import 'package:logging/logging.dart';

import '../api/webrtc_api.g.dart';
import '../util/webrtc_extension.dart';

final _log = Logger('PixelStreamingToClientMessage');

/// ID received for each type of message we can receive from the streamer via pixel streaming.
enum PixelStreamingToClientMessageKind {
  /// Indicates whether this client controls stream quality.
  qualityControlOwnership(0, ClientQualityControlOwnershipMessage.parse),

  /// Contains a response to UI interactions.
  response(1),

  /// A command to run on the client.
  command(2, ClientCommandMessage.parse),

  /// A command to freeze the stream, containing a JPEG image to display while frozen.
  freezeFrame(3),

  /// A command to unfreeze the stream.
  unfreezeFrame(4),

  /// Contains the average QP (quantization parameter) per second of the stream.
  videoEncoderAvgQP(5),

  /// Contains a JSON report of stream latency test results.
  latencyTest(6),

  /// Contains the initial Pixel Streaming settings.
  initialSettings(7),

  /// Contains the extension for a file being sent.
  fileExtension(8),

  /// Contains the MIME type for a file being sent.
  fileMimeType(9),

  /// Contains the contents of a file being sent.
  fileContents(10),

  /// A response to a TestEcho message from the client containing the message that the client sent.
  testEcho(11),

  /// Indicates whether this client controls input to the engine.
  inputControlOwnership(12),

  /// Contains the engine-assigned ID of a newly-connected controller.
  gamepadResponse(13, ClientGamepadResponseMessage.parse),

  /// Contains a JSON map from protocol message types to corresponding IDs.
  protocol(255);

  /// The ID indicating the message type.
  final int id;

  /// Function that parses the message [buffer] and produces a message object.
  /// If null, this message type can't be parsed.
  final Function(RtcDataBuffer buffer)? parse;

  /// Lookup table from message kind ID to enum value.
  static Map<int, PixelStreamingToClientMessageKind>? _lookupTable;

  /// Find the [PixelStreamingToClientMessageKind] with the given ID, or null if it doesn't exist.
  static PixelStreamingToClientMessageKind? withId(int id) {
    if (_lookupTable == null) {
      _lookupTable = {for (final value in PixelStreamingToClientMessageKind.values) value.id: value};
    }

    return _lookupTable![id];
  }

  const PixelStreamingToClientMessageKind(
    this.id, [
    this.parse,
  ]);
}

/// A message that indicates whether this client controls stream quality.
class ClientQualityControlOwnershipMessage {
  const ClientQualityControlOwnershipMessage._({required this.bControlsQuality});

  /// Whether the client controls the quality of the stream.
  final bool bControlsQuality;

  /// Parse the message from a message [buffer].
  static ClientQualityControlOwnershipMessage? parse(RtcDataBuffer buffer) {
    return ClientQualityControlOwnershipMessage._(
      bControlsQuality: buffer.byteData.getUint8(1) != 0,
    );
  }
}

/// A message that contains a command to the client.
class ClientCommandMessage {
  const ClientCommandMessage._({required this.json});

  /// Whether the client controls the quality of the stream.
  final Map<String, dynamic> json;

  /// Parse the message from a message [buffer].
  static ClientCommandMessage? parse(RtcDataBuffer buffer) {
    // We need a Uint16List to read UTF-16 strings, but our string starts at byte 1 (after the ID byte) and a Uint16List
    // offset must be a multiple of 2.
    // Instead, copy the bytes to a new Uint8List starting from byte 1, then convert that buffer to a Uint16List.
    final Uint8List bytes = Uint8List.fromList(buffer.data.buffer.asUint8List(buffer.data.offsetInBytes + 1));
    final Uint16List charCodes = bytes.buffer.asUint16List();
    final String stringData = String.fromCharCodes(charCodes);

    final dynamic json;
    try {
      json = jsonDecode(stringData);
    } catch (error) {
      _log.warning('Failed to parse WebSocket message:\n$error\nMessage text:$stringData');
      return null;
    }

    return ClientCommandMessage._(json: json);
  }
}

/// A message that contains the index of a newly connected controller.
class ClientGamepadResponseMessage {
  const ClientGamepadResponseMessage._({required this.controllerIndex});

  /// Whether the client controls the quality of the stream.
  final int controllerIndex;

  /// Parse the message from a message [buffer].
  static ClientGamepadResponseMessage? parse(RtcDataBuffer buffer) {
    return ClientGamepadResponseMessage._(
      controllerIndex: buffer.byteData.getUint8(1),
    );
  }
}
