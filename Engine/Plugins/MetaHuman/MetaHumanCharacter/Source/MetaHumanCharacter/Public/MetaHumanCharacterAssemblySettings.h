// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanTypes.h"
#include "Features/IModularFeatures.h"

#include "MetaHumanCharacterAssemblySettings.generated.h"

#define UE_API METAHUMANCHARACTER_API

// Default pipelines for selection in the tool, should be in sync with the pipelines in UMetaHumanCharacterPaletteProjectSettings
UENUM()
enum class EMetaHumanDefaultPipelineType : uint8
{
	Cinematic UMETA(DisplayName = "UE Cine (Complete)"),
	Optimized UMETA(DisplayName = "UE Optimized"),
	UEFN UMETA(DisplayName = "UEFN Export"),
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterAssemblySettings
{
	GENERATED_BODY()
	
public:
	UE_API FMetaHumanCharacterAssemblySettings();
	UE_API ~FMetaHumanCharacterAssemblySettings();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EMetaHumanDefaultPipelineType PipelineType = EMetaHumanDefaultPipelineType::Cinematic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EMetaHumanQualityLevel PipelineQuality = EMetaHumanQualityLevel::High;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FDirectoryPath RootDirectory{ TEXT("/Game/MetaHumans") };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FDirectoryPath CommonDirectory{ TEXT("/Game/MetaHumans/Common") };

	static inline const FName AnimationSystemNameAnimBP = TEXT("AnimBP");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName AnimationSystemName = AnimationSystemNameAnimBP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FString NameOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FString ArchiveName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FDirectoryPath OutputFolder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bBakeMakeup = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bExportZipFile = false;
};

class IMetaHumanCharacterAssemblySettingsExtender : public IModularFeature
{
public:
	virtual ~IMetaHumanCharacterAssemblySettingsExtender() = default;
	static inline const FName FeatureName = TEXT("MetaHumanCharacterAssemblySettingsExtender");

	virtual void PostConstructionEdit(FMetaHumanCharacterAssemblySettings* AssemblySettings) {}
};

#undef UE_API
