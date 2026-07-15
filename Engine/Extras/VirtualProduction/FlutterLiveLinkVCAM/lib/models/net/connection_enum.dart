// Copyright Epic Games, Inc. All Rights Reserved.

/// Possible states for our connection to Unreal Engine.
enum EngineConnectionState {
  /// Not connected to the engine.
  disconnected,

  /// Trying to connect to a specific engine instance.
  connecting,

  /// Partially connected to an engine, but needs a streamer ID to continue.
  waitingForStreamer,

  /// Connected to the engine and ready to stream.
  connected,
}

/// Possible results of attempting to connect to the engine.
enum EngineConnectionResult {
  /// The connection was successful.
  success,

  /// The connection was successful, but the user needs to subscribe to a streamer before continuing.
  needsStreamer,

  /// The connection was cancelled.
  cancelled,

  /// The connection sequence timed out.
  timedOut,

  /// The connection couldn't be opened because another one was already open.
  alreadyOpen,

  /// The connection failed because AR failed to initialize.
  arFailure,

  /// The WebSocket connection to the signaling server failed.
  webSocketFailure,

  /// There were no streamers available.
  noStreamers,

  /// The connection failed for unknown reasons.
  genericFailure,
}
