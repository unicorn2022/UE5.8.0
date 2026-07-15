// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.util

/** An object associated with an ID which can be used to refer to it when communicating with Flutter. */
abstract class IdObject {
    companion object {
        /** Index that indicates an IndexedObject is not managed. */
        const val invalidId: Long = -1
    }

    /** The unique ID referring to this object within its manager. */
    var id: Long = invalidId
        private set

    /** The manager that tracks this object. */
    private var manager: IdObjectManager<*>? = null

    /** Whether this has been disposed of by its manager. */
    private var bIsDisposed = false

    /**
     * Called when this is registered to a manager and provided an ID.
     * @return false if this failed to initialize, else true
     */
    fun onRegistered(id: Long, manager: IdObjectManager<*>): Boolean {
        assert(this.manager == null) { "IndexedObject initialized more than once" }

        this.id = id
        this.manager = manager

        if (!onIdReady()) {
            this.id = invalidId
            this.manager = null
            return false
        }

        return true
    }

    /** Called when this is unregistered from the manager. */
    fun onUnregistered() {
        if (bIsDisposed) {
            return
        }

        dispose()
        bIsDisposed = true
    }

    /**
     * Initialize any resources used by this object after this has registered with a manager.
     * @return false if this failed to initialize, else true
     */
    protected open fun onIdReady(): Boolean = true

    /** Dispose of any resources held by this object. */
    protected abstract fun dispose()
}
