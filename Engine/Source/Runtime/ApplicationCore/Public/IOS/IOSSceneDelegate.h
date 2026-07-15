// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#import <UIKit/UIKit.h>

#if UE_IOS_SCENE_LIFECYCLE

#ifndef SWIFT_IMPORT
APPLICATIONCORE_API
#endif
@interface IOSSceneDelegate : UIResponder <UIWindowSceneDelegate>

@property (strong, nonatomic) UIWindow *window;

@end

#endif // UE_IOS_SCENE_LIFECYCLE
