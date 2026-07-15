// Copyright Epic Games, Inc. All Rights Reserved.

import 'dart:async';
import 'dart:collection';
import 'dart:io';
import 'dart:math';

import 'package:archive/archive.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:logging/logging.dart';
import 'package:uuid/uuid.dart';

final _log = Logger('UnrealEngineBeacon');

/// Function that takes an [inputStream] containing a beacon response message, the [address] from which the message was
/// received, the [protocolVersion] reported by the engine, and the engine's unique ID, and returns data of type
/// [UserDataType] parsed from that message.
/// If this returns null, the connection is considered invalid and will be ignored.
/// If this throws an exception, it will be reported as a warning and the connection will be ignored.
typedef UnrealBeaconResponseUserDataReader<UserDataType> = UnrealBeaconResponseUserData<UserDataType>? Function(
  InputStream inputStream,
  InternetAddress address,
  int protocolVersion,
  UuidValue engineUuid,
);

/// Read a string from an [inputStream], represented as a 32-bit string length followed by a list of UTF-8 characters.
/// If [bRemoveNullTerminator] is true, remove the null terminator from the string if present.
/// Return null if the string couldn't be read.
String? readStringFromStream(InputStream inputStream, {bool bRemoveNullTerminator = true}) {
  final int nameLength = inputStream.readUint32();
  String name = inputStream.readString(size: nameLength, utf8: true);

  if (name[name.length - 1] == '\x00') {
    // Remove the null terminator
    name = name.substring(0, name.length - 1);
  }

  return name;
}

/// Convert a Uint8List to a UUID.
UuidValue _uuidFromUint8List(Uint8List data) {
  String guidString = Uuid.unparse(data);
  return UuidValue.fromString(guidString);
}

/// User-overridable data retrieved parsed from a beacon response message.
class UnrealBeaconResponseUserData<UserDataType> {
  const UnrealBeaconResponseUserData({
    this.name = null,
    this.additionalData = null,
  });

  /// User-friendly name of the connection. If null, the connection's IP address will be used.
  final String? name;

  /// Additional data retrieved from the beacon message.
  final UserDataType? additionalData;
}

/// Data about a response from Unreal Engine to a beacon message.
class UnrealBeaconResponse<UserDataType> {
  const UnrealBeaconResponse._({
    required this.protocolVersion,
    required this.uuid,
    required this.name,
    required this.address,
    required this.lastSeenTime,
    required this.additionalData,
  });

  /// Protocol version of the engine.
  final int protocolVersion;

  /// Unique identifier of the engine.
  final UuidValue uuid;

  /// User-friendly name of the engine.
  final String name;

  /// The IP address on which the engine responded.
  final InternetAddress address;

  /// When this connection last responded to a beacon message.
  final DateTime lastSeenTime;

  /// Additional data retrieved from the beacon message.
  final UserDataType additionalData;

  /// Retrieve the data from a datagram [datagram] received in response to a beacon message.
  /// If the connection data couldn't be retrieved, this will return null.
  static UnrealBeaconResponse<UserDataType>? fromDatagram<UserDataType>({
    required Datagram datagram,
    required UnrealBeaconResponseUserDataReader? userDataReader,
  }) {
    final InputStream stream = InputStream(datagram.data);
    stream.byteOrder = LITTLE_ENDIAN;
    UnrealBeaconResponse<UserDataType>? connection;

    try {
      final int protocolVersion = stream.readByte(); // Protocol version; currently ignored
      final UuidValue engineUuid = _uuidFromUint8List(stream.readBytes(16).toUint8List());

      final UnrealBeaconResponseUserData? userData = userDataReader?.call(
        stream,
        datagram.address,
        protocolVersion,
        engineUuid,
      );
      if (userDataReader != null && userData == null) {
        // Invalid connection, so ignore this.
        return null;
      }

      connection = UnrealBeaconResponse<UserDataType>._(
        protocolVersion: protocolVersion,
        uuid: engineUuid,
        name: userData?.name ?? datagram.address.address.toString(),
        address: datagram.address,
        lastSeenTime: DateTime.timestamp(),
        additionalData: userData?.additionalData,
      );
    } catch (error) {
      _log.warning('Failed to interpret beacon response from ${datagram.address.address}:/n$error');
    }

    return connection;
  }

  @override
  String toString() => '$name | ${address.address} | $uuid';
}

/// Configuration for an [UnrealEngineBeacon].
class UnrealEngineBeaconConfig {
  const UnrealEngineBeaconConfig({
    required this.beaconAddress,
    required this.beaconPort,
    required this.protocolIdentifier,
    this.protocolVersion = 0,
    this.userDataReader,
    this.beaconInterval = const Duration(seconds: 1),
    this.connectionStaleTime = const Duration(seconds: 3),
    this.beaconFailureTimeout = const Duration(seconds: 5),
    this.beaconFailureBackoff = 2.0,
  });

  /// The address to which to send multicast beacon messages.
  final InternetAddress beaconAddress;

  /// The port to which to send multicast beacon messages.
  final int beaconPort;

  /// A short string uniquely identifying this application.
  final String protocolIdentifier;

  /// The protocol version to multicast to beacon receivers.
  final int protocolVersion;

  /// An optional function which can read additional data from a beacon response message.
  final UnrealBeaconResponseUserDataReader? userDataReader;

  /// The base rate at which to send beacon messages.
  final Duration beaconInterval;

  /// How long to wait before considering a connection stale and pruning it from the list.
  final Duration connectionStaleTime;

  /// The amount of time before reporting to the user that the beacon has failed to connect.
  final Duration beaconFailureTimeout;

  /// The amount to multiply [beaconInterval] by for each consecutive failure to send a message.
  final double beaconFailureBackoff;
}

/// Sends UDP multicasts on all available interfaces to search for Unreal Engine instances and maintains a list of
/// recently seen instances.
/// [UserDataType] is the type of additional user data to read from beacon responses, if any.
class UnrealEngineBeacon<UserDataType> {
  UnrealEngineBeacon({
    required BuildContext context,
    required this.config,
    this.onBeaconFailure,
  }) {
    _log.info('Started beacon on ${config.beaconAddress.address}:${config.beaconPort}');

    _updateBeaconSockets().then((value) {
      if (_bIsDisposed) {
        return;
      }

      _startUpdateTimer();
    });
  }

  /// How long all beacons can fail before reporting it to the user.
  static const _failureTimeout = Duration(seconds: 5);

  /// The configuration for sending and receiving beacon messages.
  final UnrealEngineBeaconConfig config;

  /// Map from addresses for each network interface to our beacon socket for that interface.
  final Map<InternetAddress, _BeaconSocket> _sockets = {};

  /// Sorted list of recent responses to the beacon, excluding duplicate responses from the same engine.
  final List<UnrealBeaconResponse<UserDataType>> _responseList = [];

  /// Callback for when all beacon sockets are failing, so engines can't be detected.
  final Function()? onBeaconFailure;

  /// Timer used to fire the periodic update.
  Timer? _updateTimer;

  /// Timer used to notify the user that the beacon has been consistently failing.
  Timer? _failureTimer;

  /// If true, we already reported that all beacons are failing.
  bool _bHasReportedFailure = false;

  /// If true, this beacon has been disposed and should cancel any pending work.
  bool _bIsDisposed = false;

  /// Notifier for [responses]. When changed, listeners will receive the updated list.
  final _responsesNotifier =
      ValueNotifier<UnmodifiableListView<UnrealBeaconResponse<UserDataType>>>(UnmodifiableListView([]));

  /// Sorted list of recent responses to the beacon, excluding duplicate responses from the same engine.
  ValueListenable<UnmodifiableListView<UnrealBeaconResponse<UserDataType>>> get responses => _responsesNotifier;

  /// Clean up all sockets and stop listening for engine instances.
  void dispose() {
    _log.info('Disposed beacon on ${config.beaconAddress.address}:${config.beaconPort}');

    _stopUpdateTimer();

    for (final _BeaconSocket socket in _sockets.values) {
      socket.dispose();
    }
    _sockets.clear();

    _bIsDisposed = true;
  }

  /// Pause sending beacon messages if paused.
  void pause() {
    _stopUpdateTimer();

    for (final _BeaconSocket socket in _sockets.values) {
      socket.bIsPaused = true;
    }
  }

  /// Resume sending beacon messages if paused.
  void resume() {
    _startUpdateTimer();
    _pruneConnections();

    for (final _BeaconSocket socket in _sockets.values) {
      socket.bIsPaused = false;
    }
  }

  /// Start/restart the update timer.
  void _startUpdateTimer() {
    _stopUpdateTimer();

    _updateTimer = Timer.periodic(config.beaconInterval, _onUpdatePeriod);
  }

  /// Stop the update timer.
  void _stopUpdateTimer() {
    if (_updateTimer != null) {
      _updateTimer!.cancel();
      _updateTimer = null;
    }
  }

  /// Called on each update period.
  void _onUpdatePeriod([Timer? timer]) {
    _checkForBeaconSocketFailure();
    _pruneConnections();
    _updateBeaconSockets();
  }

  /// Check if all beacon sockets are failing, and if so, report it.
  void _checkForBeaconSocketFailure() {
    bool bAllFailing = true;
    for (final _BeaconSocket socket in _sockets.values) {
      if (!socket.bIsFailing) {
        bAllFailing = false;
      }
    }

    if (bAllFailing) {
      if (!_bHasReportedFailure && _failureTimer == null) {
        _failureTimer = Timer(_failureTimeout, () {
          _bHasReportedFailure = true;

          _log.info(
            'Engine beacon on ${config.beaconAddress.address}:${config.beaconPort} failed '
            '(${_sockets.length} beacon sockets tried)',
          );

          onBeaconFailure?.call();
          _failureTimer = null;
        });
      }
    } else {
      _failureTimer?.cancel();
      _failureTimer = null;

      _bHasReportedFailure = false;
    }
  }

  /// Bind/unbind beacon sockets in order to match the current list of network interfaces.
  Future<void> _updateBeaconSockets() async {
    final Map<InternetAddress, String> adapterNames = {};
    final Set<InternetAddress> addresses = {};

    // Get the list of all available network interfaces
    final List<NetworkInterface> interfaces = await NetworkInterface.list();

    if (_bIsDisposed) {
      return;
    }

    for (final NetworkInterface interface in interfaces) {
      addresses.addAll(interface.addresses);

      for (final InternetAddress address in interface.addresses) {
        adapterNames[address] = interface.name;
      }
    }

    // Close beacons we no longer need
    final List<InternetAddress> staleAddresses = [];
    for (final InternetAddress existingAddress in _sockets.keys) {
      if (!addresses.contains(existingAddress)) {
        staleAddresses.add(existingAddress);
      }
    }

    for (final InternetAddress staleAddress in staleAddresses) {
      _sockets[staleAddress]?.dispose();
      _sockets.remove(staleAddress);
    }

    // Create beacons for new interfaces
    for (final InternetAddress newAddress in addresses) {
      if (_sockets.containsKey(newAddress)) {
        continue;
      }

      final String adapterName = adapterNames[newAddress] ?? 'unknown';
      _log.info('New adapter address available: $adapterName (${newAddress.address})');

      final beaconSocket = _BeaconSocket<UserDataType>(
        config: config,
        adapterName: adapterName,
        address: newAddress,
        onNewConnectionData: _updateConnection,
      );

      _sockets[newAddress] = beaconSocket;
    }
  }

  /// Update the data for a new or existing connection.
  void _updateConnection(UnrealBeaconResponse<UserDataType> newConnectionData) {
    bool bAlreadyExisted = false;

    // Check if we've already seen this connection
    for (int i = 0; i < _responseList.length; ++i) {
      final UnrealBeaconResponse<UserDataType> oldConnectionData = _responseList[i];
      if (oldConnectionData.uuid == newConnectionData.uuid) {
        // Refresh the connection data (including last seen time)
        _responseList[i] = newConnectionData;

        bAlreadyExisted = true;
        break;
      }
    }

    if (!bAlreadyExisted) {
      _log.info('New connection available: $newConnectionData');
      _responseList.add(newConnectionData);
    }

    _responseList.sort((a, b) => a.name.compareTo(b.name));
    _responsesNotifier.value = UnmodifiableListView(_responseList);
  }

  /// Remove any stale connections from the list
  void _pruneConnections() {
    final DateTime now = DateTime.now();
    final List<int> toRemove = [];

    for (int i = 0; i < _responseList.length; ++i) {
      if (now.difference(_responseList[i].lastSeenTime) > config.connectionStaleTime) {
        toRemove.add(i);
      }
    }

    for (final int i in toRemove.reversed) {
      _log.info('Connection pruned: ${_responseList[i]}');
      _responseList.removeAt(i);
    }

    if (toRemove.length > 0) {
      _responsesNotifier.value = UnmodifiableListView(_responseList);
    }
  }
}

/// A socket open on a specific interface to send and receive beacon messages.
class _BeaconSocket<UserDataType> {
  _BeaconSocket({
    required this.config,
    required this.adapterName,
    required this.address,
    required this.onNewConnectionData,
  }) {
    _bindSocket().then((final bool bIsValid) {
      if (!bIsValid) {
        return;
      }

      /// Send the first message immediately so we don't have to wait for an update
      _sendBeaconMessage();
    });
  }

  /// The configuration for sending and receiving beacon messages.
  final UnrealEngineBeaconConfig config;

  /// The name of the adapter this is using.
  final String adapterName;

  /// The address of the interface to send/listen on.
  final InternetAddress address;

  /// Callback function when new engine connection data has been received.
  final Function(UnrealBeaconResponse<UserDataType>) onNewConnectionData;

  /// Timer for sending out beacon messages.
  Timer? _messageTimer;

  /// Timer for when we consider this beacon to have failed.
  Timer? _failTimer;

  /// Whether the beacon has recently successfully sent a multicast message.
  bool _bHasRecentlySucceeded = false;

  /// Whether this has been disposed.
  bool _bWasDisposed = false;

  /// Whether this is temporarily not sending beacon messages.
  bool _bIsPaused = false;

  /// Whether this encountered an instant failure code and should not attempt to reconnect.
  bool _bWasInstantFailed = false;

  /// The socket used to send and receive beacon messages.
  RawDatagramSocket? _socket;

  /// The subscription by which this listens to [_socket].
  StreamSubscription? _subscription;

  /// The number of times this has consecutively failed to send a message via its socket.
  int _consecutiveFailures = 0;

  /// Error codes which, if encountered, should cause the beacon to immediately be treated as failed.
  static Set<int> get _instantFailErrorCodes {
    if (Platform.isIOS) {
      return {
        // Operation not permitted
        1,
      };
    }

    if (Platform.isAndroid) {
      return {
        // Network is unreachable
        101,
      };
    }

    return {};
  }

  /// Whether this is currently failing to send messages to its interface.
  bool get bIsFailing => !_bHasRecentlySucceeded;

  /// Whether this is currently prevented from sending beacon messages.
  bool get bIsPaused => _bIsPaused;

  void set bIsPaused(bool bNewValue) {
    _bIsPaused = bNewValue;
    if (_bIsPaused) {
      _stopUpdateTimer();
    } else {
      _startUpdateTimer();
    }
  }

  /// Stop sending and receiving for this beacon and close its socket.
  void dispose() {
    if (_bWasDisposed) {
      return;
    }

    _log.info('Disposed beacon at $adapterName (${address.address})');

    _bWasDisposed = true;
    _giveUp();
  }

  /// Update the number of consecutive failures and adjust the timer accordingly.
  void _updateConsecutiveFailures(int newValue) {
    if (_consecutiveFailures != newValue) {
      _consecutiveFailures = newValue;
      _startUpdateTimer();
    }
  }

  /// Start sending periodic beacon messages.
  /// If [bLogPeriod] is true, log how long we'll wait until the next message.
  void _startUpdateTimer() {
    if (_messageTimer != null) {
      _messageTimer!.cancel();
    }

    // Send periodic beacons to discover any new Unreal Engine instances
    final Duration period = config.beaconInterval * pow(config.beaconFailureBackoff, _consecutiveFailures);
    _messageTimer = Timer.periodic(period, (_) => _sendBeaconMessage());
  }

  /// Stop sending periodic beacon messages.
  void _stopUpdateTimer() {
    _messageTimer?.cancel();
    _messageTimer = null;
  }

  /// Send a beacon message on this socket.
  void _sendBeaconMessage() {
    if (_socket != null) {
      final Uint8List message = _makeBeaconMessage();
      if (_socket!.send(message, config.beaconAddress, config.beaconPort) > 0) {
        // We successfully sent a beacon message, so cancel the failure timer and indicate that we should consider it a
        // new failure if we fail again later.
        _bHasRecentlySucceeded = true;
        _failTimer?.cancel();
        _updateConsecutiveFailures(0);
      }
    }
  }

  /// Bind the socket used to send and receive beacon messages.
  Future<bool> _bindSocket() async {
    _stopUpdateTimer();

    _subscription?.cancel();
    _socket?.close();

    late final RawDatagramSocket newSocket;

    try {
      newSocket = await RawDatagramSocket.bind(address, 0, ttl: 4);
      _log.info('Beacon socket open on "$adapterName" (${address.address})');
    } catch (error) {
      _log.warning('Failed to bind socket for "$adapterName" (${address.address}):\n$error');
      return Future.value(false);
    }

    _socket = newSocket;
    _subscription = newSocket.listen(
      _receiveMessage,
      onError: _onSocketError,
      onDone: _onSocketClosed,
      cancelOnError: false,
    );

    if (!_bIsPaused) {
      _startUpdateTimer();
    }

    return Future.value(true);
  }

  /// Called when an error occurs on the socket that sends beacon messages.
  void _onSocketError(Object error, StackTrace stackTrace) {
    _log.info('Failed to send beacon message on "$adapterName" (${address.address}):\n$error');

    if (error is SocketException && _instantFailErrorCodes.contains(error.osError?.errorCode)) {
      _log.info('Error code ${error.osError?.errorCode} is an instant fail; closing beacon');
      _bWasInstantFailed = true;
      _giveUp();
      return;
    }

    _updateConsecutiveFailures(_consecutiveFailures + 1);

    if (_bHasRecentlySucceeded && _failTimer == null) {
      _failTimer = Timer(config.beaconFailureTimeout, _handleBeaconFailed);
    }
  }

  /// Called when the beacon has failed for too long.
  void _handleBeaconFailed() {
    _bHasRecentlySucceeded = false;
    _failTimer = null;
  }

  /// Receive a message on the beacon socket (presumably a reply to a beacon message).
  void _receiveMessage(RawSocketEvent event) {
    final Datagram? datagram = _socket!.receive();
    if (datagram != null) {
      final UnrealBeaconResponse<UserDataType>? newConnection = UnrealBeaconResponse.fromDatagram(
        datagram: datagram,
        userDataReader: config.userDataReader,
      );

      if (newConnection != null) {
        onNewConnectionData(newConnection);
      }
    }
  }

  /// Called when the socket closes.
  void _onSocketClosed() async {
    if (_bWasDisposed || _bWasInstantFailed) {
      return;
    }

    _stopUpdateTimer();
    _log.info('Socket to $adapterName (${address.address}) closed; attempting to re-bind');

    final bIsBound = await _bindSocket();

    if (!bIsBound) {
      _log.info('Failed to re-bind to $adapterName (${address.address}); closing beacon');

      _giveUp();
      return;
    }

    _log.info('Socket to $adapterName (${address.address}) re-bound');
  }

  /// Close the socket and cancel the subscription if necessary, then report failure.
  void _giveUp() {
    _subscription?.cancel();
    _socket?.close();
    _socket = null;
    _bHasRecentlySucceeded = false;
  }

  /// Make the beacon message that will be multicast to find compatible engine instances.
  Uint8List _makeBeaconMessage() {
    return Uint8List.fromList(config.protocolIdentifier.codeUnits + [config.protocolVersion]);
  }
}
