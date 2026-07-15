// Copyright Epic Games, Inc. All Rights Reserved.


#include "Animation/AnimationInstanceScope.h"

namespace UE::Anim
{
IMPLEMENT_NOTIFY_CONTEXT_INTERFACE(FAnimNotifyAssetPlayerInstanceContext)

FAnimNotifyAssetPlayerInstanceContext::FAnimNotifyAssetPlayerInstanceContext(const uint32 InAssetPlayerInstanceID)
	: AssetPlayerInstanceID(InAssetPlayerInstanceID)
{
}
}
