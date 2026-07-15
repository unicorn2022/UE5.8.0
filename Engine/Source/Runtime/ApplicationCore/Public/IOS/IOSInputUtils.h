// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#import <UIKit/UIKit.h>
#import <UIKit/UIKeyConstants.h>

namespace IOS
{
	enum class EUnrealKeyCode : uint32;
	
	/** Translate UI Kit USB Hid keycodes into Unreal Keycodes */
	EUnrealKeyCode TranslateUSBHIDToUnrealKeycode(UIKeyboardHIDUsage HIDKeycode);
}

