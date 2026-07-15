// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterMaterialSet.h"
#include "MetaHumanTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "MetaHumanCharacterSkin.generated.h"

class UMaterialInstanceConstant;

USTRUCT(BlueprintType)
struct FMetaHumanCharacterSkinProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float U = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float V = 0.5f;

	UPROPERTY(VisibleAnywhere, Category = "Skin")
	FVector3f BodyBias = {74.f, 28.f, 15.f};

	UPROPERTY(VisibleAnywhere, Category = "Skin")
	FVector3f BodyGain = {30.f, 10.f, 5.f};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bShowTopUnderwear = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "8", ClampMin = "0", ClampMax = "8"))
	int32 BodyTextureIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	int32 FaceTextureIndex = 0;

	// Roughness UI Multiply. Range from 0.85 to 1.15
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (UIMin = "0", UIMax = "1", ClampMin = "0.85", ClampMax = "1.15"))
	float Roughness = 1.06f;

	// Palm Props
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float PalmLightness = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float PalmTint = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "0.5"))
	float PalmCavityDarkness = 0.15f;

	// Fingernail Props
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet")
	FLinearColor FingernailTintColor = FLinearColor{ 0.083333f, 0.f, 0.004665f, 1.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float FingernailTintIntensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float FingernailMetallic = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float FingernailRoughness = 0.5f;

	// Toenail Props
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet")
	FLinearColor ToenailTintColor = FLinearColor{ 0.083333f, 0.f, 0.004665f, 1.f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ToenailTintIntensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ToenailMetallic = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hands & Feet", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ToenailRoughness = 0.5f;

	/** Returns true if the properties that drive texture synthesis are equal.
	 * Used to skip re-synthesis when only non-texture properties (e.g. roughness, nail/palm settings) changed.
	 * Keep in sync with UMetaHumanCharacterEditorSkinTool::Setup() WatchProperty -> UpdateSkinSynthesizedTexture bindings. */
	METAHUMANCHARACTER_API bool EqualForTextureSynthesis(const FMetaHumanCharacterSkinProperties& InOther) const;
	METAHUMANCHARACTER_API bool operator==(const FMetaHumanCharacterSkinProperties& InOther) const;
	METAHUMANCHARACTER_API bool operator!=(const FMetaHumanCharacterSkinProperties& InOther) const;
};

UENUM()
enum class EMetaHumanCharacterFrecklesMask : uint8
{
	None,
	Type1,
	Type2,
	Type3,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterFrecklesMask, EMetaHumanCharacterFrecklesMask::Count);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterFrecklesProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Density = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Strength = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Saturation = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freckles", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float ToneShift = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freckles")
	EMetaHumanCharacterFrecklesMask Mask = EMetaHumanCharacterFrecklesMask::None;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterAccentRegionProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Redness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Saturation = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Lightness = 0.5f;
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterAccentRegions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Scalp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Forehead;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Nose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties UnderEye;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Cheeks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Lips;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Chin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents")
	FMetaHumanCharacterAccentRegionProperties Ears;
};

UENUM()
enum class EMetaHumanCharacterSkinPreviewMaterial : uint8
{
	Default		UMETA(DisplayName = "Topology"),
	Editable	UMETA(DisplayName = "Skin"),
	Clay		UMETA(DisplayName = "Clay"),
	Count UMETA(Hidden),
	Gray UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterSkinPreviewMaterial, EMetaHumanCharacterSkinPreviewMaterial::Count);

/**
 * Struct that hard references to all possible textures used in the skin material.
 * This is also used as a utility to pass around skin textures sets
 */
USTRUCT()
struct FMetaHumanCharacterSkinTextureSet
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<EFaceTextureType, TObjectPtr<class UTexture2D>> Face;

	UPROPERTY()
	TMap<EBodyTextureType, TObjectPtr<class UTexture2D>> Body;

	/**
	 * Appends another texture set to this one.
	 * Replaces or adds any new textures from InOther
	 */
	METAHUMANCHARACTER_API void Append(const FMetaHumanCharacterSkinTextureSet& InOther);
};

/**
 * Struct used to hold soft references to a skin texture set. This is
 * used to store override textures in the MetaHuman Character object
 * which are not loaded by default.
 */
USTRUCT(BlueprintType)
struct FMetaHumanCharacterSkinTextureSoftSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Textures")
	TMap<EFaceTextureType, TSoftObjectPtr<class UTexture2D>> Face;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Textures")
	TMap<EBodyTextureType, TSoftObjectPtr<class UTexture2D>> Body;

	/**
	 * Load the textures and returns a texture set
	 */
	METAHUMANCHARACTER_API FMetaHumanCharacterSkinTextureSet LoadTextureSet() const;
};

UENUM(BlueprintType)
enum class EMetaHumanCharacterTeethAndEyesSlot : uint8
{
	EyeLeft,
	EyeRight,
	EyeShell,
	LacrimalFluid,
	Teeth,
	Eyelashes,
	EyelashesHiLods,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EMetaHumanCharacterTeethAndEyesSlot, EMetaHumanCharacterTeethAndEyesSlot::Count);

/**
 * Struct used to hold soft references to skin material overrides.
 * When enabled, these materials replace the default preview MICs
 * used for face and body rendering.
 */
USTRUCT(BlueprintType)
struct FMetaHumanCharacterMaterialOverrideSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials|Face")
	TMap<EMetaHumanCharacterSkinMaterialSlot, TSoftObjectPtr<UMaterialInstanceConstant>> Skin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials|Face", DisplayName = "Teeth & Eyes")
	TMap<EMetaHumanCharacterTeethAndEyesSlot, TSoftObjectPtr<UMaterialInstanceConstant>> TeethAndEyes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials|Body")
	TSoftObjectPtr<UMaterialInstanceConstant> Body;

	METAHUMANCHARACTER_API bool operator==(const FMetaHumanCharacterMaterialOverrideSet& InOther) const;
	METAHUMANCHARACTER_API bool operator!=(const FMetaHumanCharacterMaterialOverrideSet& InOther) const;
};

/**
 * Enum with the valid texture resolutions to request from the service
 */
UENUM()
enum class ERequestTextureResolution : int32
{
	Res2k = 2048 UMETA(DisplayName = "2k"),
	Res4k = 4096 UMETA(DisplayName = "4k"),
	Res8k = 8192 UMETA(DisplayName = "8k"),
};
ENUM_RANGE_BY_VALUES(ERequestTextureResolution, ERequestTextureResolution::Res2k, ERequestTextureResolution::Res4k, ERequestTextureResolution::Res8k);

USTRUCT(BlueprintType)
struct FMetaHumanCharacterTextureSourceResolutions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceAlbedo = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceNormal = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceCavity = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	ERequestTextureResolution FaceAnimatedMaps = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyAlbedo = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyNormal = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyCavity = ERequestTextureResolution::Res2k;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	ERequestTextureResolution BodyMasks = ERequestTextureResolution::Res2k;

	/**
	 * @brief Sets all resolutions to be the same
	 */
	METAHUMANCHARACTER_API void SetAllResolutionsTo(ERequestTextureResolution InResolution);

	/**
	 * @brief Returns true if all resolutions are the same as InResolution
	 */
	METAHUMANCHARACTER_API bool AreAllResolutionsEqualTo(ERequestTextureResolution InResolution) const;
};

/** Holds texture and material override settings */
USTRUCT(BlueprintType)
struct FMetaHumanCharacterTextureMaterialOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Overrides")
	bool bEnableTextureOverrides = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Overrides", meta = (EditCondition = "bEnableTextureOverrides"))
	FMetaHumanCharacterSkinTextureSoftSet TextureOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Overrides")
	bool bEnableMaterialOverrides = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Overrides", meta = (EditCondition = "bEnableMaterialOverrides"))
	FMetaHumanCharacterMaterialOverrideSet MaterialOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Overrides", DisplayName = "Inherit UI Parameters and Source Textures", meta = (EditCondition = "bEnableMaterialOverrides",
		ToolTip = "When enabled, UI material parameters (Skin, Eyes, Makeup, Teeth & Eyelashes) and source textures are applied on top of the override materials. When disabled, override materials are used as-is."))
	bool bInheritUIParamsAndSrcTextures = false;

	/** Returns true if UI material parameters and source textures should be applied to the current materials. */
	bool ShouldInheritUIParamsAndSrcTextures() const
	{
		return !bEnableMaterialOverrides || bInheritUIParamsAndSrcTextures;
	}
};

USTRUCT(BlueprintType)
struct FMetaHumanCharacterSkinSettings
{
	GENERATED_BODY()

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FMetaHumanCharacterSkinSettings() = default;
	FMetaHumanCharacterSkinSettings(const FMetaHumanCharacterSkinSettings& Other) = default;
	FMetaHumanCharacterSkinSettings(FMetaHumanCharacterSkinSettings&& Other) = default;
	FMetaHumanCharacterSkinSettings& operator=(const FMetaHumanCharacterSkinSettings& Other) = default;
	FMetaHumanCharacterSkinSettings& operator=(FMetaHumanCharacterSkinSettings&& Other) = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterSkinProperties Skin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Freckles", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterFrecklesProperties Freckles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (ShowOnlyInnerProperties))
	FMetaHumanCharacterAccentRegions Accents;

	// Desired resolutions to request when Downloading Source Textures
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Sources")
	FMetaHumanCharacterTextureSourceResolutions DesiredTextureSourcesResolutions;

	UE_DEPRECATED(5.8, "bEnableTextureOverrides is deprecated, use TextureMaterialOverrides instead.")
	UPROPERTY(BlueprintReadWrite, Category = "Texture Overrides", meta = (DeprecatedProperty, DeprecationMessage = "bEnableTextureOverrides is deprecated, use TextureMaterialOverrides instead."))
	bool bEnableTextureOverrides = false;

	UE_DEPRECATED(5.8, "TextureOverrides is deprecated, use TextureMaterialOverrides instead.")
	UPROPERTY(BlueprintReadWrite, Category = "Texture Overrides", meta = (DeprecatedProperty, DeprecationMessage = "TextureOverrides is deprecated, use TextureMaterialOverrides instead."))
	FMetaHumanCharacterSkinTextureSoftSet TextureOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture & Material Overrides")
	FMetaHumanCharacterTextureMaterialOverrides TextureMaterialOverrides;

	/**
	 * Returns a texture set considering the TextureMaterialOverrides.bEnableTextureOverrides flag.
	 * If the flag is enabled any texture in TextureMaterialOverrides.TextureOverrides are going to be
	 * present in the returned texture set
	 */
	METAHUMANCHARACTER_API FMetaHumanCharacterSkinTextureSet GetFinalSkinTextureSet(const FMetaHumanCharacterSkinTextureSet& InSkinTextureSet) const;

	/** Migrates deprecated properties to TextureMaterialOverrides */
	METAHUMANCHARACTER_API void PostSerialize(const FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FMetaHumanCharacterSkinSettings> : public TStructOpsTypeTraitsBase2<FMetaHumanCharacterSkinSettings>
{
	enum
	{
		WithPostSerialize = true
	};
};
