// Copyright Epic Games, Inc. All Rights Reserved.

#include "MutableAssetUserDataModule.h"

#include "MeshAddAssetUserData.h"
#include "MuCO/ICustomizableObjectModule.h"


IMPLEMENT_MODULE(FMutableAssetUserDataModule, MutableAssetUserData);


void FMutableAssetUserDataModule::StartupModule()
{
	ICustomizableObjectModule& Module = ICustomizableObjectModule::Get();

	Module.RegisterExternalOperation(FMeshAddAssetUserData::StaticStruct());
}


void FMutableAssetUserDataModule::ShutdownModule()
{
	ICustomizableObjectModule& Module = ICustomizableObjectModule::Get();

	Module.UnregisterExternalOperation(FMeshAddAssetUserData::StaticStruct());
}
