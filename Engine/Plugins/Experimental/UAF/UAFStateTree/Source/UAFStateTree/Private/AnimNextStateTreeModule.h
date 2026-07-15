// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAnimNextStateTreeModule.h"
#include "UObject/TopLevelAssetPath.h"

namespace UE::UAF::StateTree
{

class FAnimNextStateTreeModule : public IAnimNextStateTreeModule
{
public:
	virtual void ShutdownModule() override;
	virtual void StartupModule() override;
	
private:
	FTopLevelAssetPath UAFStateTreeClassPath;
};

}
