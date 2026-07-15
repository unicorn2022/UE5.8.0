// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.util

/**
 * Maintains a map of IDs to a corresponding [IdObject], enabling lookup by their IDs.
 */
open class IdObjectManager<T : IdObject>(
    private val debugTypeName: String
) {
    /** Map from index to a managed object. */
    private val objectMap: MutableMap<Long, T> = mutableMapOf()

    /** The index to use for the next created object. */
    private var nextIndex: Long = 0

    /** Start tracking a new object instance. */
    fun register(newObject: T): Long {
        if (!newObject.onRegistered(nextIndex, this)) {
            return IdObject.invalidId
        }

        objectMap[nextIndex] = newObject

        return nextIndex++
    }

    /**
     * Dispose of an object instance created in this manager.
     * @param id The ID of the object.
     * @return The object that was disposed, if any.
     */
    fun unregister(id: Long): T? {
        val removed = objectMap.remove(id)
        removed?.onUnregistered()
        return removed
    }

    /**
     * Dispose of all object instances in this manager.
     */
    fun disposeAll() {
        while (objectMap.isNotEmpty()) {
            objectMap.values.first().onUnregistered()
        }
    }

    /**
     * Get an object managed by this manager.
     * @param id The index of the object.
     * @return The object with that ID, if any.
     */
    fun get(id: Long): T? {
        return objectMap[id]
    }

    /**
     * Get an object managed by this manager and throw an exception if it doesn't exist.
     * @param id The index of the object.
     * @return The object with that ID.
     */
    fun getChecked(id: Long): T {
        return get(id) ?: throw Exception("No $debugTypeName with ID $id")
    }
}
