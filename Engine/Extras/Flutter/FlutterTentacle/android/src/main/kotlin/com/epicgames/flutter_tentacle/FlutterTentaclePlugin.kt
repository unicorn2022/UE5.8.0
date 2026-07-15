// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.flutter_tentacle

import com.epicgames.flutter_tentacle.api.FlutterTentacleApi
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result

/** Registers the Flutter Pigeon API when the plugin is loaded. */
class FlutterTentaclePlugin: FlutterPlugin, MethodCallHandler {
	/** API that interfaces with Tentacle devices. */
	lateinit var tentacleApi: FlutterTentacleApi
		private set

	override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
		tentacleApi = FlutterTentacleApi(flutterPluginBinding.binaryMessenger, flutterPluginBinding.applicationContext)
	}

	override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
	}
	
	override fun onMethodCall(call: MethodCall, result: Result) {
	}
}
