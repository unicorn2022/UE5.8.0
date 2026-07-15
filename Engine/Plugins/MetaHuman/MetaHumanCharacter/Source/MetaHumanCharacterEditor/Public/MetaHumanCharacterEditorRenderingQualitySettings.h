// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"

#include "MetaHumanCharacterEditorRenderingQualitySettings.generated.h"

USTRUCT(BlueprintType)
struct FMetaHumanCharacterViewportRenderingFlags
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	bool bTransmissionForAllLights = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "RenderingQuality")
	bool bTonemapper = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "RenderingQuality")
	bool bDepthOfField = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category = "RenderingQuality")
	bool bDynamicShadows = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	bool bSubsurfaceScattering = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	bool bGlobalIllumination = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	bool bTemporalAA = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	bool bLumenGlobalIllumination = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	bool bLumenReflections = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	bool bPreviewingScreenPercentage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality", meta = (EditCondition = "bPreviewingScreenPercentage"))
	int32 PreviewScreenPercentage = 50;

	static FMetaHumanCharacterViewportRenderingFlags Epic;
	static FMetaHumanCharacterViewportRenderingFlags High;
	static FMetaHumanCharacterViewportRenderingFlags Medium;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterRenderingQualityProfile
{
	GENERATED_BODY()

	explicit FMetaHumanCharacterRenderingQualityProfile(const FString& InProfileName = TEXT(""));
	FMetaHumanCharacterRenderingQualityProfile(const FString& InProfileName, const bool bInMetaHumanCharacterDefault, const FMetaHumanCharacterViewportRenderingFlags& InViewportRenderingFlags);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality", meta = (EditCondition = "!bMetaHumanCharacterDefault", HideEditConditionToggle))
	FString ProfileName;

	/** True if it's a not a user generated profile aka default profile Epic, High or Medium, else false. Default profiles are not serialized into config */
	UPROPERTY()
	bool bMetaHumanCharacterDefault = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	FPostProcessSettings PostProcessSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RenderingQuality")
	FMetaHumanCharacterViewportRenderingFlags ViewportRenderingFlags;

	static const FMetaHumanCharacterRenderingQualityProfile Invalid;
	static FMetaHumanCharacterRenderingQualityProfile Epic;
	static FMetaHumanCharacterRenderingQualityProfile High;
	static FMetaHumanCharacterRenderingQualityProfile Medium;
};

UCLASS()
class UMetaHumanCharacterRenderingQualityProfileProxy : public UObject
{
	GENERATED_BODY()

public:
	//~Begin UObject Interface
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject Interface

	void SetActiveProfile(const FMetaHumanCharacterRenderingQualityProfile& InProfile);

	UPROPERTY(EditAnywhere, Category = "RenderingQuality", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterRenderingQualityProfile RenderingQualityProfile;

	int32 ActiveProfileIndex = INDEX_NONE;
	FString CachedProfileName;

	/** The delegate executed when ProfileName is changed */
	FSimpleMulticastDelegate OnProfileNameUpdated;

	/** The delegate executed when PostProcessSettings or ViewportRenderingFlags is modified */
	FSimpleMulticastDelegate OnProfileUpdated;
};
