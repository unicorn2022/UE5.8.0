// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.live_link_vcam.gamepad.api

import FlutterGamepadError
import GamepadFlutterApi
import GamepadHostApi
import GamepadInput
import GamepadInputEvent
import GamepadInputType
import android.content.Context
import android.hardware.input.InputManager
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.core.content.ContextCompat.getSystemService
import com.epicgames.live_link_vcam.util.FlutterPluginApi
import com.epicgames.live_link_vcam.util.IFlutterPluginApi
import io.flutter.plugin.common.BinaryMessenger
import kotlin.math.abs

/**
 * Handles messages about Gamepad devices to and from the Flutter API.
 * See https://developer.android.com/develop/ui/views/touch-and-input/game-controllers/controller-input
 */
class FlutterGamepadApi(
    /** Messenger used to communicate with Flutter. */
    binaryMessenger: BinaryMessenger,

    /** Context in which this was created. */
    context: Context,
) : GamepadHostApi, IFlutterPluginApi<GamepadFlutterApi> by FlutterPluginApi(GamepadFlutterApi(binaryMessenger)),
    InputManager.InputDeviceListener {
    /** List of gamepad inputs treated as buttons but which derive their actual input from axis values. */
    private val axisButtons = listOf(
        GamepadInput.DPADUP,
        GamepadInput.DPADDOWN,
        GamepadInput.DPADLEFT,
        GamepadInput.DPADRIGHT,
    )

    /** Map from motion event axes to corresponding gamepad input axes to send to Flutter. */
    private val gamepadAxes = mapOf(
        MotionEvent.AXIS_X to GamepadInput.THUMBSTICKLEFTX,
        MotionEvent.AXIS_Y to GamepadInput.THUMBSTICKLEFTY,
        MotionEvent.AXIS_Z to GamepadInput.THUMBSTICKRIGHTX,
        MotionEvent.AXIS_RZ to GamepadInput.THUMBSTICKRIGHTY,
        MotionEvent.AXIS_LTRIGGER to GamepadInput.TRIGGERAXISLEFT,
        MotionEvent.AXIS_BRAKE to GamepadInput.TRIGGERAXISLEFT,
        MotionEvent.AXIS_RTRIGGER to GamepadInput.THUMBSTICKRIGHTX,
        MotionEvent.AXIS_GAS to GamepadInput.TRIGGERAXISRIGHT,
    )

    /** Set of axes for which to reverse the input in order to match Unreal's expected input range. */
    private val reversedAxes = setOf(
        MotionEvent.AXIS_Y,
        MotionEvent.AXIS_RZ,
    )

    /** List of IDs for active input devices that we detected as gamepads. */
    private val activeGamepadIds = mutableSetOf<Int>()

    /**
     * Map from device index to list of inputs in axisButtons which are currently in the "down" state.
     */
    private val downAxisButtonsByDevice = mutableMapOf<Int, Set<GamepadInput>>()

    /**
     * Map from device index to map from gamepad axis to the last value sent to Flutter.
     */
    private val gamepadAxisValuesByDevice = mutableMapOf<Int, MutableMap<GamepadInput, Float>>()

    /** Set up messaging with Flutter. */
    init {
        GamepadHostApi.setUp(binaryMessenger, this)

        val inputManager: InputManager = getSystemService(context, InputManager::class.java)
            ?: throw FlutterGamepadError("Failed to retrieve input manager")

        inputManager.inputDeviceIds.forEach { deviceId -> onInputDeviceAdded(deviceId) }

        inputManager.registerInputDeviceListener(this, null)
    }

    /** Called by the activity when a motion event occurs. */
    fun dispatchGenericMotionEvent(event: MotionEvent?): Boolean {
        if (event == null || !activeGamepadIds.contains(event.deviceId)) {
            // Don't consume invalid events or events originating from non-gamepad devices
            return false
        }

        // Get the new D-pad button states
        val currentDownAxisButtons: Set<GamepadInput>? = downAxisButtonsByDevice[event.deviceId]
        val newDownAxisButtons = mutableSetOf<GamepadInput>()

        when (event.getAxisValue(MotionEvent.AXIS_HAT_X)) {
            -1f -> newDownAxisButtons.add(GamepadInput.DPADLEFT)
            1f -> newDownAxisButtons.add(GamepadInput.DPADRIGHT)
        }

        when (event.getAxisValue(MotionEvent.AXIS_HAT_Y)) {
            -1f -> newDownAxisButtons.add(GamepadInput.DPADUP)
            1f -> newDownAxisButtons.add(GamepadInput.DPADDOWN)
        }

        // Send button events converted from axes
        for (button in axisButtons) {
            val bWasDown: Boolean = currentDownAxisButtons?.contains(button) ?: false
            val bIsDown: Boolean = newDownAxisButtons.contains(button)

            if (bWasDown == bIsDown) {
                continue
            }

            callFlutter { flutter ->
                flutter.onGamepadInputEvent(
                    GamepadInputEvent(
                        event.deviceId.toLong(),
                        button,
                        GamepadInputType.BUTTON,
                        if (bIsDown) 1.0 else 0.0
                    )
                ) {}
            }
        }

        // Update cached button states from axes
        downAxisButtonsByDevice[event.deviceId] = newDownAxisButtons

        // Handle axis inputs reported directly to Flutter
        val newGamepadAxisValues = mutableMapOf<GamepadInput, Float>()
        for (axisPair in gamepadAxes) {
            val axis: Int = axisPair.key
            val input: GamepadInput = axisPair.value

            var newValue: Float = event.getAxisValue(axis)
            if (reversedAxes.contains(axis)) {
                newValue = -newValue
            }

            val existingValue: Float? = newGamepadAxisValues[input]

            if (existingValue != null && abs(newValue) < abs(existingValue)) {
                // If multiple axes are mapped to the same gamepad input, take the one with the highest absolute value
                continue
            }

            newGamepadAxisValues[input] = newValue
        }

        val gamepadAxisValues = gamepadAxisValuesByDevice.getOrPut(event.deviceId) { mutableMapOf() }
        for (input in gamepadAxes.values) {
            val newAxisValue: Float = newGamepadAxisValues[input]!!

            // Don't send unchanged axes
            if (newAxisValue == gamepadAxisValues.getOrDefault(input, 0)) {
                continue
            }

            // Update cached value
            gamepadAxisValues[input] = newAxisValue

            // Send new value to flutter
            callFlutter { flutter ->
                flutter.onGamepadInputEvent(
                    GamepadInputEvent(
                        event.deviceId.toLong(),
                        input,
                        GamepadInputType.AXIS,
                        newAxisValue.toDouble()
                    )
                ) {}
            }
        }

        return true
    }

    /** Called by the activity when a key event occurs. */
    fun dispatchKeyEvent(event: KeyEvent?): Boolean {
        if (event == null || !activeGamepadIds.contains(event.deviceId)) {
            // Don't consume invalid events or events originating from non-gamepad devices
            return false
        }

        if (event.source == InputDevice.SOURCE_JOYSTICK) {
            // Joystick and D-pad events both use this source, so to disambiguate them, we only want to handle them
            // in dispatchGenericMotionEvent
            return true
        }

        if (event.repeatCount > 0) {
            // Ignore repeat events
            return true
        }

        val input: GamepadInput = when (event.keyCode) {
            KeyEvent.KEYCODE_BUTTON_A -> GamepadInput.FACEBUTTONBOTTOM
            KeyEvent.KEYCODE_BUTTON_B -> GamepadInput.FACEBUTTONRIGHT
            KeyEvent.KEYCODE_BUTTON_X -> GamepadInput.FACEBUTTONLEFT
            KeyEvent.KEYCODE_BUTTON_Y -> GamepadInput.FACEBUTTONTOP
            KeyEvent.KEYCODE_BUTTON_L1 -> GamepadInput.SHOULDERBUTTONLEFT
            KeyEvent.KEYCODE_BUTTON_R1 -> GamepadInput.SHOULDERBUTTONRIGHT
            KeyEvent.KEYCODE_BUTTON_L2 -> GamepadInput.TRIGGERBUTTONLEFT
            KeyEvent.KEYCODE_BUTTON_R2 -> GamepadInput.TRIGGERBUTTONRIGHT
            KeyEvent.KEYCODE_BUTTON_THUMBL -> GamepadInput.THUMBSTICKLEFTBUTTON
            KeyEvent.KEYCODE_BUTTON_THUMBR -> GamepadInput.THUMBSTICKRIGHTBUTTON
            KeyEvent.KEYCODE_BUTTON_SELECT -> GamepadInput.SPECIALBUTTONLEFT
            KeyEvent.KEYCODE_BUTTON_START -> GamepadInput.SPECIALBUTTONRIGHT
            else -> return false
        }

        val value: Double = when (event.action) {
            KeyEvent.ACTION_UP -> 0.0
            KeyEvent.ACTION_DOWN -> 1.0
            else -> return false
        }

        callFlutter { flutter ->
            flutter.onGamepadInputEvent(
                GamepadInputEvent(
                    event.deviceId.toLong(),
                    input,
                    GamepadInputType.BUTTON,
                    value
                )
            ) {}
        }

        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_L2 || event.keyCode == KeyEvent.KEYCODE_BUTTON_R2) {
            // Triggers fire both button and axis events, so don't consume the input as a button only
            return false
        }

        return true
    }

    //region InputManager.InputDeviceListener
    override fun onInputDeviceAdded(deviceId: Int) {
        if (!isGamepad(deviceId)) {
            return
        }

        onGamepadConnected(deviceId)
    }

    override fun onInputDeviceRemoved(deviceId: Int) {
        if (!activeGamepadIds.contains(deviceId)) {
            return
        }

        onGamepadDisconnected(deviceId)
    }

    override fun onInputDeviceChanged(deviceId: Int) {
        val bIsNowGamepad = isGamepad(deviceId)
        val bWasGamepad = activeGamepadIds.contains(deviceId)

        if (bIsNowGamepad && !bWasGamepad) {
            onGamepadConnected(deviceId)
        } else if (!bIsNowGamepad && !bWasGamepad) {
            onGamepadDisconnected(deviceId)
        }
    }
    //endregion

    //region GamepadHostApi
    override fun getActiveGamepadIds(): List<Long> {
        return activeGamepadIds.map { deviceId ->
            deviceId.toLong()
        }
    }
    //endregion

    /** Called when a gamepad is connected. */
    private fun onGamepadConnected(deviceId: Int) {
        activeGamepadIds.add(deviceId)

        callFlutter { flutter ->
            flutter.onGamepadConnected(deviceId.toLong()) {}
        }
    }

    /** Called when a gamepad is disconnected. */
    private fun onGamepadDisconnected(deviceId: Int) {
        activeGamepadIds.remove(deviceId)

        callFlutter { flutter ->
            flutter.onGamepadDisconnected(deviceId.toLong()) {}
        }
    }

    /** Check if the device with the given ID is a gamepad. */
    private fun isGamepad(deviceId: Int): Boolean {
        // Input sources that identify a gamepad
        val device = InputDevice.getDevice(deviceId) ?: return false

        if (device.sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD) {
            return true
        }

        return device.sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
    }
}
