// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPtr.h"

#include "DNACommon.h"

#define UE_API INTERCHANGEDNA_API

class IDNAReader;


class FInterchangeDnaModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;

	static UE_API FInterchangeDnaModule& GetModule();

	/** Set DNA asset user data and map joints for skeletal mesh */
	UE_DEPRECATED(5.8, "This function has been deprecated, use CreateAndAttachDNAToSkeletalMesh instead")
	UE_API void SetSkelMeshDNAData(TNotNull<class USkeletalMesh*> InSkelMesh, TSharedPtr<IDNAReader> InDNAReader);

	/** Create DNA Asset and attach it to skeletal mesh via DNAAssetUserData */
	UE_API void CreateAndAttachDNAToSkeletalMesh(TNotNull<class USkeletalMesh*> InSkelMesh, TSharedPtr<IDNAReader> InDNAReader);

	/** DNAInterchangeModule implementation */
	UE_API class USkeletalMesh* ImportSync(const FString& InNewRigAssetName, const FString& InNewRigPath, TSharedPtr<IDNAReader> InDNAReader, TSoftObjectPtr<class USkeleton> InSkeleton, const FDNAConfig& DNAConfig = FDNAConfig());
	
};

#undef UE_API
