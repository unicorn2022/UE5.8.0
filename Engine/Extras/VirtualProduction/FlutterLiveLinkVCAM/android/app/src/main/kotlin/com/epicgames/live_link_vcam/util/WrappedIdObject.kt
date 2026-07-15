// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.util

/** A thin wrapper for an object that allows it to be registered and looked up by its ID. */
open class WrappedIdObject<T>(val inner: T) : IdObject() {
    override fun dispose() {}
}
