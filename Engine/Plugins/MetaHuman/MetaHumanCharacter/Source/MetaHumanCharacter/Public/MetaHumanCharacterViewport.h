// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"
#include "AssetRegistry/AssetData.h"

#include "MetaHumanCharacterViewport.generated.h"


UENUM()
enum class EMetaHumanCharacterEnvironment : uint8
{
	Studio,
	Split,
	Fireside,
	Moonlight,
	Tungsten,
	Portrait,
	RedLantern,
	TextureBooth,

	Count UMETA(Hidden),

	// Custom and Default are not real environments. They live after Count so TEnumRange skips them.
	// Custom means the character is using a lighting world picked from the Custom Light Presets list.
	// Default means use whatever the project setting "Default Lighting Environment" is set to.
	Custom,
	Default,
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterEnvironment, EMetaHumanCharacterEnvironment::Count);

UENUM()
enum class EMetaHumanCharacterLOD : uint8
{
	LOD0,
	LOD1,
	LOD2,
	LOD3,
	LOD4,
	LOD5,
	LOD6,
	LOD7,
	Auto,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterLOD, EMetaHumanCharacterLOD::Count);


UENUM()
enum class EMetaHumanCharacterCameraFrame : uint8
{
	Auto,
	Face,
	Body,
	Far,
	Hands,
	Feet,
	SelectedPoint,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterCameraFrame, EMetaHumanCharacterCameraFrame::Count);


UENUM()
enum class EMetaHumanCharacterRenderingQuality : uint8
{
	Medium,
	High,
	Epic,
	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterRenderingQuality, EMetaHumanCharacterRenderingQuality::Count);


USTRUCT(BlueprintType)
struct FMetaHumanCharacterViewportSettings
{
	GENERATED_BODY()

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FMetaHumanCharacterViewportSettings() = default;
	FMetaHumanCharacterViewportSettings(const FMetaHumanCharacterViewportSettings& Other) = default;
	FMetaHumanCharacterViewportSettings(FMetaHumanCharacterViewportSettings&& Other) = default;
	FMetaHumanCharacterViewportSettings& operator=(const FMetaHumanCharacterViewportSettings& Other) = default;
	FMetaHumanCharacterViewportSettings& operator=(FMetaHumanCharacterViewportSettings&& Other) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	EMetaHumanCharacterEnvironment CharacterEnvironment = EMetaHumanCharacterEnvironment::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	TSoftObjectPtr<UWorld> CustomLightingEnvironment;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	FString CustomLightingEnvironmentKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	FLinearColor BackgroundColor = FLinearColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport", meta = (UIMin = "-270", UIMax = "270", ClampMin = "-270", ClampMax = "270"))
	float LightRotation = 0.0f;

	UE_DEPRECATED(5.8, "bTonemapperEnabled is deprecated. Tonemapper is now controlled via RenderingQualityProfiles in UMetaHumanCharacterEditorSettings.")
	UPROPERTY(BlueprintReadWrite, Transient, Category = "Viewport", meta = (DeprecatedProperty, DeprecationMessage = "bTonemapperEnabled is deprecated. Tonemapper is now controlled via RenderingQualityProfiles in UMetaHumanCharacterEditorSettings."))
	bool bTonemapperEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	EMetaHumanCharacterLOD LevelOfDetail = EMetaHumanCharacterLOD::LOD0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	EMetaHumanCharacterCameraFrame CameraFrame = EMetaHumanCharacterCameraFrame::Face;

	/** When enabled, scales the perspective camera speed based on the distance between the camera and its look-at position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport", Transient)
	bool bUseDistanceScaledCameraSpeed = true;

	UE_DEPRECATED(5.8, "RenderingQuality is deprecated, please use RenderingQualityProfileIndex instead.")
	UPROPERTY(BlueprintReadWrite, Transient, Category = "Viewport", meta = (DeprecatedProperty, DeprecationMessage = "RenderingQuality is deprecated, please use RenderingQualityProfileIndex instead."))
	EMetaHumanCharacterRenderingQuality RenderingQuality = EMetaHumanCharacterRenderingQuality::Epic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	int32 RenderingQualityProfileIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bAlwaysUseHairCards = false;

	UPROPERTY(Transient)
	bool bHairHiddenInPreviewScene = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowViewportOverlays = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceBones = false;

	UPROPERTY(EditAnywhere, Category = "Viewport", meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float FaceBoneSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyBones = false;

	UPROPERTY(EditAnywhere, Category = "Viewport", meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float BodyBoneSize = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceNormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyNormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceTangents = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyTangents = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowFaceBinormals = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport")
	bool bShowBodyBinormals = false;
};