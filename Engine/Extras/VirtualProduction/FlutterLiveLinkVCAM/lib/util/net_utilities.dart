// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:io';

import 'package:archive/archive.dart';
import 'package:epic_common/unreal_beacon.dart';

/// The configuration to use for the beacon that detects compatible Unreal Engine instances.
final unrealEngineBeaconConfig = UnrealEngineBeaconConfig(
  beaconAddress: InternetAddress('230.0.0.3'),
  beaconPort: 6667,
  protocolIdentifier: 'UEVCam',
  userDataReader: (InputStream inputStream, InternetAddress address, _, __) {
    final int port = inputStream.readUint32();
    final bool bCanStream = inputStream.readByte() == 1;

    String? name = readStringFromStream(inputStream);

    // If the engine didn't provide a friendly name, just use the address and Pixel Streaming port.
    if (name == null || name.isEmpty || (name.length == 1 && name[0] == '\x00')) {
      name = '${address.address}:${port.toString()}';
    }

    return UnrealBeaconResponseUserData(
      additionalData: VcamBeaconData(
        pixelStreamingPort: port,
        bCanStream: bCanStream,
      ),
      name: name,
    );
  },
);

/// Type of response received from beacon messages for this app.
typedef VcamBeaconResponse = UnrealBeaconResponse<VcamBeaconData>;

/// Additional VCAM-specific data received from beacon messages.
class VcamBeaconData {
  const VcamBeaconData({
    required this.pixelStreamingPort,
    required this.bCanStream,
  });

  /// Port of the Pixel Streaming signaling server.
  final int pixelStreamingPort;

  /// Whether the engine's Pixel Streaming server is ready to stream.
  final bool bCanStream;
}

/// Data about a connection to Unreal Engine.
class EngineConnectionData {
  const EngineConnectionData({
    required this.name,
    required this.pixelStreamingAddress,
    required this.pixelStreamingPort,
  })  : bIsValid = true,
        bIsDemo = false;

  /// Hidden constructor where hidden properties can be set.
  const EngineConnectionData._({
    required this.name,
    required this.pixelStreamingAddress,
    required this.pixelStreamingPort,
    this.bIsValid = true,
    this.bIsDemo = false,
  });

  /// Create connection data for demo mode.
  static EngineConnectionData forDemoMode() {
    return EngineConnectionData._(
      name: 'Demo',
      pixelStreamingAddress: InternetAddress('127.0.0.1'),
      pixelStreamingPort: 0,
      bIsDemo: true,
    );
  }

  /// An invalid engine connection.
  static EngineConnectionData get invalid => EngineConnectionData._(
        name: '',
        pixelStreamingAddress: InternetAddress.anyIPv4,
        pixelStreamingPort: 0,
        bIsValid: false,
      );

  /// Create connection data from a [response] to an UnrealEngineBeacon message.
  static EngineConnectionData fromBeaconResponse(VcamBeaconResponse response) {
    return EngineConnectionData(
      name: response.name,
      pixelStreamingAddress: response.address,
      pixelStreamingPort: response.additionalData.pixelStreamingPort,
    );
  }

  /// Load engine connection data from JSON.
  /// Throws an exception if it can't be loaded from the provided data.
  static EngineConnectionData fromJson(Map<String, dynamic> json) {
    final String? name = json['Name'];
    if (name == null) {
      throw Exception('No name provided');
    }

    final String? addressString = json['Address'];
    if (addressString == null) {
      throw Exception('No address provided');
    }

    final InternetAddress? address = InternetAddress.tryParse(addressString);
    if (address == null) {
      throw Exception('Failed to parse address');
    }

    final int? port = json['Port'];
    if (port == null) {
      throw Exception('No port provided');
    }

    return EngineConnectionData._(
      name: name,
      pixelStreamingAddress: address,
      pixelStreamingPort: port,
      bIsDemo: json['IsDemo'],
      bIsValid: json['IsValid'],
    );
  }

  /// Whether this data is valid.
  final bool bIsValid;

  /// Whether this connection is a demo, meaning no actual Unreal Engine instance exists.
  final bool bIsDemo;

  /// User-friendly name of the connection.
  final String name;

  /// Address of the Pixel Streaming signaling server.
  final InternetAddress pixelStreamingAddress;

  /// Port of the Pixel Streaming signaling server.
  final int pixelStreamingPort;

  Map<String, dynamic> toJson() {
    return {
      'Name': name,
      'Address': pixelStreamingAddress.address,
      'Port': pixelStreamingPort,
      'IsDemo': bIsDemo,
      'IsValid': bIsValid,
    };
  }
}
