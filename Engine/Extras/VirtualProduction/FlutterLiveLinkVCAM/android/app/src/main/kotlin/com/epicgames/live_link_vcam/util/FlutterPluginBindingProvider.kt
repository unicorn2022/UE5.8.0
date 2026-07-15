// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.util

import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.FlutterPlugin.FlutterPluginBinding

/**
 * Plugin which exposes the Flutter plugin binding for access to e.g. its texture binding facilities.
 */
class FlutterPluginBindingProvider : FlutterPlugin {
    private var _binding: FlutterPluginBinding? = null

    /** The plugin binding used to communicate with the Flutter engine. */
    val binding: FlutterPluginBinding? get() = _binding

    //region FlutterPlugin
    override fun onAttachedToEngine(binding: FlutterPluginBinding) {
        this._binding = binding
    }

    override fun onDetachedFromEngine(binding: FlutterPluginBinding) {
        this._binding = null
    }
    //endregion FlutterPlugin
}
