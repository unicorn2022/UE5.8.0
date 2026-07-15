// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorAssetTypeActions.h"

#include "DisplayClusterConfiguratorUtils.h"
#include "DisplayClusterRootActor.h"

UClass* FDisplayClusterConfiguratorActorAssetTypeActions::GetSupportedClass() const
{
	return ADisplayClusterRootActor::StaticClass();
}
