// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Components/ComponentInterfaces.h"

class FActorSkinnedMeshComponentInterface : public ISkinnedMeshComponent
{
public:
#if WITH_EDITOR
	virtual void PostAssetCompilation() override;
	virtual void PostSkeletalHierarchyChange() override;
#endif
	virtual USkinnedAsset* GetSkinnedAsset() const override;
	virtual IPrimitiveComponent* GetPrimitiveComponentInterface() override;
};
